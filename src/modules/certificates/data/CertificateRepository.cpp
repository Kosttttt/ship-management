#include "modules/certificates/data/CertificateRepository.h"

#include "core/InstallationContext.h"
#include "modules/certificates/domain/CertificateValidation.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent set by every repository so far.
const QString kSystemUser = QStringLiteral("SYSTEM");

const QString kColumns = QStringLiteral(
    "id, vessel_id, name, category, certificate_number, applies_to,"
    " issue_date, expiry_date, issued_by, place_of_issue, is_interim,"
    " requires_annual_survey, requires_intermediate_survey, intermediate_mode, notes,"
    " list_number");

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

// Calendar dates are stored as YYYY-MM-DD text, never as an instant
// (CLAUDE.md §6.3). An invalid QDate stores as NULL, which is how
// "does not expire" is represented.
QVariant dateToStorage(const QDate& date)
{
    return date.isValid() ? QVariant(date.toString(Qt::ISODate))
                          : QVariant(QMetaType(QMetaType::QString));
}

QDate dateFromStorage(const QVariant& value)
{
    return value.isNull() ? QDate() : QDate::fromString(value.toString(), Qt::ISODate);
}

// A blank optional field is stored as NULL rather than an empty string, so
// "not entered" stays distinguishable — the convention every table uses.
QVariant nullableText(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

Certificate certificateFromRow(const QSqlQuery& query)
{
    Certificate certificate;
    certificate.id                = query.value(0).toString();
    certificate.vesselId          = query.value(1).toString();
    certificate.name              = query.value(2).toString();
    certificate.category          = CertificateCodes::categoryFromCode(query.value(3).toString());
    certificate.certificateNumber = query.value(4).toString();
    certificate.appliesTo         = query.value(5).toString();
    certificate.issueDate         = dateFromStorage(query.value(6));
    certificate.expiryDate        = dateFromStorage(query.value(7));
    certificate.issuedBy          = query.value(8).toString();
    certificate.placeOfIssue      = query.value(9).toString();
    certificate.isInterim         = query.value(10).toBool();
    certificate.requiresAnnualSurvey       = query.value(11).toBool();
    certificate.requiresIntermediateSurvey = query.value(12).toBool();
    certificate.intermediateMode =
        CertificateCodes::intermediateModeFromCode(query.value(13).toString());
    certificate.notes      = query.value(14).toString();
    certificate.listNumber = query.value(15).toString();
    return certificate;
}

// Binds the editable values, in the order both the INSERT and the UPDATE list
// them, so the two statements cannot drift apart.
void bindEditableValues(QSqlQuery& query, const Certificate& certificate)
{
    query.addBindValue(nullableText(certificate.listNumber.trimmed()));
    query.addBindValue(certificate.name.trimmed());
    query.addBindValue(CertificateCodes::categoryToCode(certificate.category));
    query.addBindValue(nullableText(certificate.certificateNumber.trimmed()));
    query.addBindValue(nullableText(certificate.appliesTo.trimmed()));
    query.addBindValue(dateToStorage(certificate.issueDate));
    query.addBindValue(dateToStorage(certificate.expiryDate));
    query.addBindValue(nullableText(certificate.issuedBy.trimmed()));
    query.addBindValue(nullableText(certificate.placeOfIssue.trimmed()));
    query.addBindValue(certificate.isInterim ? 1 : 0);
    query.addBindValue(certificate.requiresAnnualSurvey ? 1 : 0);
    query.addBindValue(certificate.requiresIntermediateSurvey ? 1 : 0);
    query.addBindValue(CertificateCodes::intermediateModeToCode(certificate.intermediateMode));
    query.addBindValue(nullableText(certificate.notes.trimmed()));
}

} // namespace

CertificateRepository::CertificateRepository(QSqlDatabase&              database,
                                             const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
{
}

bool CertificateRepository::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool CertificateRepository::list(const QString& vesselId, QList<Certificate>* certificates)
{
    m_errorString.clear();
    certificates->clear();

    QString sql = QStringLiteral("SELECT %1 FROM certificate"
                                 " WHERE vessel_id = ? AND is_deleted = 0")
                      .arg(kColumns);

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND vessel_id = ?");
    }
    sql += QStringLiteral(" ORDER BY name COLLATE NOCASE");

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(vesselId);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the certificate list:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        certificates->append(certificateFromRow(query));
    }
    return true;
}

bool CertificateRepository::findById(const QString& id, std::optional<Certificate>* certificate)
{
    m_errorString.clear();
    certificate->reset();

    QString sql = QStringLiteral("SELECT %1 FROM certificate WHERE id = ? AND is_deleted = 0")
                      .arg(kColumns);

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND vessel_id = ?");
    }

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(id);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the certificate:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        *certificate = certificateFromRow(query);
    }
    return true;
}

bool CertificateRepository::listDeletedIds(const QString& vesselId, QSet<QString>* ids)
{
    m_errorString.clear();
    ids->clear();

    // The one question list() cannot answer, because it exists to hide these
    // rows: has this id been here and been deleted?
    // certificate-import-export-spec §5.1 needs the difference, so that a
    // re-imported row whose certificate was deleted in the meantime reads as
    // "New (previously deleted)" rather than looking like a lost match. Read
    // once per import, the same way the live list is, and scope-filtered the
    // same way too.
    QString sql = QStringLiteral("SELECT id FROM certificate"
                                 " WHERE vessel_id = ? AND is_deleted = 1");

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND vessel_id = ?");
    }

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(vesselId);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the deleted certificates:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        ids->insert(query.value(0).toString());
    }
    return true;
}

bool CertificateRepository::checkVesselAllowed(const QString& vesselId)
{
    if (vesselId.isEmpty()) {
        m_errorString = QStringLiteral("A certificate must belong to a vessel.");
        return false;
    }
    if (isScopedToOneVessel() && vesselId != m_installation.vesselScope()) {
        m_errorString =
            QStringLiteral("This installation may only manage its own vessel's certificates.");
        return false;
    }
    return true;
}

bool CertificateRepository::validate(const Certificate& certificate)
{
    // The rules themselves live in the domain layer, so that CSV import's
    // preview can ask the same question without attempting a write
    // (certificate-import-export-spec §5.5). This stays as the write path's
    // own gate: every create() and update() still goes through it, and a
    // refusal still lands in errorString() for the dialog to show.
    return validateCertificate(certificate, &m_errorString);
}

bool CertificateRepository::create(const Certificate& certificate, QString* newId)
{
    m_errorString.clear();

    if (!checkVesselAllowed(certificate.vesselId) || !validate(certificate)) {
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate"
        " (id, vessel_id, list_number, name, category, certificate_number, applies_to,"
        "  issue_date, expiry_date, issued_by, place_of_issue, is_interim,"
        "  requires_annual_survey, requires_intermediate_survey, intermediate_mode, notes,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(certificate.vesselId);
    bindEditableValues(query, certificate);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(m_installation.nodeId());

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the certificate:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool CertificateRepository::update(const Certificate& certificate)
{
    m_errorString.clear();

    if (certificate.id.isEmpty()) {
        m_errorString =
            QStringLiteral("This certificate has no identifier, so it cannot be saved.");
        return false;
    }
    if (!checkVesselAllowed(certificate.vesselId) || !validate(certificate)) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE certificate SET"
        "  list_number = ?,"
        "  name = ?, category = ?, certificate_number = ?, applies_to = ?,"
        "  issue_date = ?, expiry_date = ?, issued_by = ?, place_of_issue = ?, is_interim = ?,"
        "  requires_annual_survey = ?, requires_intermediate_survey = ?,"
        "  intermediate_mode = ?, notes = ?,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    bindEditableValues(query, certificate);
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(certificate.id);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the certificate:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This certificate no longer exists and could not be saved.");
        return false;
    }

    return true;
}

bool CertificateRepository::softDeleteWithChildren(const QString& id)
{
    m_errorString.clear();

    if (id.isEmpty()) {
        m_errorString = QStringLiteral("No certificate was given to delete.");
        return false;
    }

    // Loaded first, both to apply the VESSEL-mode scope filter — findById()
    // already does, which is why this is asked of it rather than written
    // again — and to be sure the row is really there before anything is
    // written.
    std::optional<Certificate> existing;
    if (!findById(id, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This certificate is no longer in the database.");
        return false;
    }

    if (!m_database.transaction()) {
        m_errorString = QStringLiteral("Could not start a transaction:\n%1")
                            .arg(m_database.lastError().text());
        return false;
    }

    const QString now = nowUtc();

    // The children first, then the parent. The order is not what §4 lists —
    // it names the certificate first — but it is the order the statements have
    // to run in, because a child write that looked its parent up would not
    // find a row already marked deleted. Inside one transaction the two orders
    // are indistinguishable from outside, and the end state is exactly §4's.
    const QStringList childTables = {QStringLiteral("endorsement"),
                                     QStringLiteral("certificate_extension"),
                                     QStringLiteral("certificate_attachment")};

    for (const QString& table : childTables) {
        QSqlQuery child(m_database);
        child.prepare(QStringLiteral("UPDATE %1 SET"
                                     "  is_deleted = 1, updated_at = ?, updated_by = ?,"
                                     "  revision = revision + 1"
                                     " WHERE certificate_id = ? AND is_deleted = 0")
                          .arg(table));
        child.addBindValue(now);
        child.addBindValue(kSystemUser);
        child.addBindValue(id);

        if (!child.exec()) {
            m_errorString = QStringLiteral("Could not delete the certificate:\n%1")
                                .arg(child.lastError().text());
            m_database.rollback();
            return false;
        }
    }

    QSqlQuery parent(m_database);
    parent.prepare(QStringLiteral(
        "UPDATE certificate SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    parent.addBindValue(now);
    parent.addBindValue(kSystemUser);
    parent.addBindValue(id);

    if (!parent.exec()) {
        m_errorString =
            QStringLiteral("Could not delete the certificate:\n%1").arg(parent.lastError().text());
        m_database.rollback();
        return false;
    }

    if (parent.numRowsAffected() == 0) {
        // Deleted by something else between the lookup above and here. Nothing
        // of this transaction is kept, so the children are not orphaned.
        m_errorString = QStringLiteral("This certificate is no longer in the database.");
        m_database.rollback();
        return false;
    }

    if (!m_database.commit()) {
        m_errorString =
            QStringLiteral("Could not delete the certificate:\n%1").arg(m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    return true;
}

QString CertificateRepository::errorString() const
{
    return m_errorString;
}
