#include "modules/certificates/data/CertificateAttachmentRepository.h"

#include "core/InstallationContext.h"

#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

const QString kSystemUser = QStringLiteral("SYSTEM");

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

CertificateAttachment attachmentFromRow(const QSqlQuery& query)
{
    CertificateAttachment attachment;
    attachment.id            = query.value(0).toString();
    attachment.certificateId = query.value(1).toString();
    attachment.relativePath  = query.value(2).toString();
    return attachment;
}

} // namespace

CertificateAttachmentRepository::CertificateAttachmentRepository(
    QSqlDatabase& database, const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
{
}

bool CertificateAttachmentRepository::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool CertificateAttachmentRepository::findActiveFor(
    const QString& certificateId, std::optional<CertificateAttachment>* attachment)
{
    m_errorString.clear();
    attachment->reset();

    if (certificateId.isEmpty()) {
        return true; // nothing asked for, nothing found — not an error
    }

    // The join is what makes the scope filter possible at all: the vessel a
    // certificate belongs to is the only thing that says whether this
    // installation may see its attachments.
    QString sql = QStringLiteral(
        "SELECT a.id, a.certificate_id, a.relative_path"
        "  FROM certificate_attachment a"
        "  JOIN certificate c ON c.id = a.certificate_id"
        " WHERE a.certificate_id = ? AND a.is_deleted = 0 AND c.is_deleted = 0");

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND c.vessel_id = ?");
    }

    // At most one is active at a time, enforced by attach(). Ordering
    // newest-first anyway means a row that somehow slipped past that — a
    // future sync merging two sides, say — resolves to the latest rather than
    // to whichever the database happened to return first.
    sql += QStringLiteral(" ORDER BY a.created_at DESC LIMIT 1");

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(certificateId);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the certificate's attachment:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        *attachment = attachmentFromRow(query);
    }
    return true;
}

QString CertificateAttachmentRepository::vesselIdForCertificate(const QString& certificateId,
                                                                bool*          ok)
{
    *ok = false;

    if (certificateId.isEmpty()) {
        m_errorString = QStringLiteral("An attachment must belong to a certificate.");
        return QString();
    }

    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("SELECT vessel_id FROM certificate WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(certificateId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the certificate:\n%1").arg(query.lastError().text());
        return QString();
    }

    if (!query.next()) {
        m_errorString = QStringLiteral("That certificate no longer exists, so a document cannot "
                                       "be attached to it.");
        return QString();
    }

    const QString vesselId = query.value(0).toString();

    if (isScopedToOneVessel() && vesselId != m_installation.vesselScope()) {
        m_errorString =
            QStringLiteral("This installation may only manage its own vessel's certificates.");
        return QString();
    }

    *ok = true;
    return vesselId;
}

bool CertificateAttachmentRepository::attach(const QString& certificateId,
                                             const QString& relativePath,
                                             QString*       newId)
{
    m_errorString.clear();

    bool ok = false;
    vesselIdForCertificate(certificateId, &ok);
    if (!ok) {
        return false;
    }

    if (relativePath.isEmpty()) {
        m_errorString = QStringLiteral("An attachment needs the path of the stored file.");
        return false;
    }

    // A path that is absolute on this machine would be wrong on the other side
    // of a sync (CLAUDE.md §6.9). Refused here rather than quietly stored,
    // because by the time anyone noticed, the folder would already have been
    // copied to another machine.
    if (QDir::isAbsolutePath(relativePath)) {
        m_errorString = QStringLiteral("An attachment path must be relative to the managed data "
                                       "folder, not absolute.");
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    // Both statements or neither: a soft delete without the replacement row
    // would leave the certificate looking as though it had never had a
    // document, while its file sat in the archive.
    if (!m_database.transaction()) {
        m_errorString = QStringLiteral("Could not start a transaction:\n%1")
                            .arg(m_database.lastError().text());
        return false;
    }

    // Soft delete, never a hard DELETE (CLAUDE.md §6.4). The replaced row is
    // what records that the archived file on disk was once the real document;
    // §10 defers a screen for browsing them precisely because it survives.
    QSqlQuery retire(m_database);
    retire.prepare(QStringLiteral(
        "UPDATE certificate_attachment SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE certificate_id = ? AND is_deleted = 0"));
    retire.addBindValue(now);
    retire.addBindValue(kSystemUser);
    retire.addBindValue(certificateId);

    if (!retire.exec()) {
        m_errorString = QStringLiteral("Could not replace the previous attachment:\n%1")
                            .arg(retire.lastError().text());
        m_database.rollback();
        return false;
    }

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO certificate_attachment"
        " (id, certificate_id, relative_path,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    insert.addBindValue(id);
    insert.addBindValue(certificateId);
    insert.addBindValue(relativePath);
    insert.addBindValue(now);
    insert.addBindValue(kSystemUser);
    insert.addBindValue(now);
    insert.addBindValue(kSystemUser);
    insert.addBindValue(m_installation.nodeId());

    if (!insert.exec()) {
        m_errorString =
            QStringLiteral("Could not save the attachment:\n%1").arg(insert.lastError().text());
        m_database.rollback();
        return false;
    }

    if (!m_database.commit()) {
        m_errorString =
            QStringLiteral("Could not save the attachment:\n%1").arg(m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

QString CertificateAttachmentRepository::errorString() const
{
    return m_errorString;
}
