#include "modules/certificates/data/CertificateExtensionRepository.h"

#include "core/InstallationContext.h"

#include <QDateTime>
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

QVariant nullableText(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

CertificateExtension extensionFromRow(const QSqlQuery& query)
{
    CertificateExtension extension;
    extension.id            = query.value(0).toString();
    extension.certificateId = query.value(1).toString();
    extension.expiryDate    = QDate::fromString(query.value(2).toString(), Qt::ISODate);
    extension.note          = query.value(3).toString();
    return extension;
}

} // namespace

CertificateExtensionRepository::CertificateExtensionRepository(
    QSqlDatabase& database, const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
{
}

bool CertificateExtensionRepository::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool CertificateExtensionRepository::findActiveFor(const QString&                       certificateId,
                                                   std::optional<CertificateExtension>* extension)
{
    m_errorString.clear();
    extension->reset();

    if (certificateId.isEmpty()) {
        return true; // nothing asked for, nothing found — not an error
    }

    // The join is what makes the scope filter possible at all: the vessel a
    // certificate belongs to is the only thing that says whether this
    // installation may see its extensions.
    QString sql = QStringLiteral(
        "SELECT x.id, x.certificate_id, x.expiry_date, x.note"
        "  FROM certificate_extension x"
        "  JOIN certificate c ON c.id = x.certificate_id"
        " WHERE x.certificate_id = ? AND x.is_deleted = 0 AND c.is_deleted = 0");

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND c.vessel_id = ?");
    }

    // At most one is active at a time (§2), enforced by start(). Ordering
    // newest-first anyway means a row that somehow slipped past that — a
    // future sync merging two sides, say — resolves to the latest rather than
    // to whichever the database happened to return first.
    sql += QStringLiteral(" ORDER BY x.created_at DESC LIMIT 1");

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(certificateId);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the certificate's short-term extension:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        *extension = extensionFromRow(query);
    }
    return true;
}

QString CertificateExtensionRepository::vesselIdForCertificate(const QString& certificateId,
                                                               bool*          ok)
{
    *ok = false;

    if (certificateId.isEmpty()) {
        m_errorString = QStringLiteral("A short-term extension must belong to a certificate.");
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
        m_errorString = QStringLiteral("That certificate no longer exists, so a short-term "
                                       "extension cannot be recorded against it.");
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

bool CertificateExtensionRepository::validate(const CertificateExtension& extension)
{
    if (!extension.expiryDate.isValid()) {
        m_errorString = QStringLiteral("An extension needs its own expiry date.");
        return false;
    }
    return true;
}

bool CertificateExtensionRepository::start(const CertificateExtension& extension, QString* newId)
{
    m_errorString.clear();

    // §5's validation order, so a friendly message always comes before any
    // raw constraint could fire.
    bool ok = false;
    vesselIdForCertificate(extension.certificateId, &ok);
    if (!ok) {
        return false;
    }
    if (!validate(extension)) {
        return false;
    }

    // The backstop for "at most one active at a time" (§2). The UI never
    // offers Start while one is active, but the repository does not rely on
    // that — the same defensive pattern used everywhere else here.
    std::optional<CertificateExtension> existing;
    if (!findActiveFor(extension.certificateId, &existing)) {
        return false;
    }
    if (existing.has_value()) {
        m_errorString = QStringLiteral("This certificate already has an active short-term "
                                       "extension. Resolve it before starting another.");
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate_extension"
        " (id, certificate_id, expiry_date, note,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(extension.certificateId);
    // A calendar date, YYYY-MM-DD (CLAUDE.md §6.3). Bound unconditionally,
    // unlike the endorsement equivalent: validate() has already refused an
    // invalid one, and the column is NOT NULL.
    query.addBindValue(extension.expiryDate.toString(Qt::ISODate));
    query.addBindValue(nullableText(extension.note.trimmed()));
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(m_installation.nodeId());

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the short-term extension:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool CertificateExtensionRepository::resolve(const QString& certificateId)
{
    m_errorString.clear();

    // Scope is checked even though this only ever narrows an UPDATE: another
    // vessel's certificate must be refused with a message, not silently
    // affect zero rows and then report success (§8 case 8).
    bool ok = false;
    vesselIdForCertificate(certificateId, &ok);
    if (!ok) {
        return false;
    }

    // Soft delete, never a hard DELETE — CLAUDE.md §6.4. Here that is not
    // only the house rule: a resolved extension is a real document from an
    // outside authority, and §9 defers a screen for browsing them precisely
    // because these rows survive.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE certificate_extension SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE certificate_id = ? AND is_deleted = 0"));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(certificateId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not resolve the short-term extension:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    return true; // affecting zero rows (nothing active) is not a failure
}

QString CertificateExtensionRepository::errorString() const
{
    return m_errorString;
}
