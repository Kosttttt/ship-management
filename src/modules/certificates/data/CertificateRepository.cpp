#include "modules/certificates/data/CertificateRepository.h"

#include "core/InstallationContext.h"

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
    if (certificate.name.trimmed().isEmpty()) {
        m_errorString = QStringLiteral("A certificate name is required.");
        return false;
    }
    if (certificate.category == CertificateCategory::Unset) {
        m_errorString = QStringLiteral("A category is required.");
        return false;
    }
    if (!certificate.issueDate.isValid()) {
        m_errorString = QStringLiteral("An issue date is required.");
        return false;
    }

    // The form's validator refuses the keystroke, so this only fires for a
    // value that arrived another way — a CSV import, in a later step.
    if (!CertificateListNumber::isValid(certificate.listNumber.trimmed())) {
        m_errorString = QStringLiteral(
            "\"%1\" is not a valid number.\n\n"
            "A number is digits, optionally followed by letters — 15, 3A, 15D.")
                            .arg(certificate.listNumber);
        return false;
    }

    // The friendly version of the migration's CHECK constraint. The constraint
    // stays as the backstop; this is the path a user actually sees.
    if (certificate.neverExpires()
        && (certificate.requiresAnnualSurvey || certificate.requiresIntermediateSurvey)) {
        m_errorString = QStringLiteral(
            "A certificate with no expiry cannot require a survey.\n\n"
            "Surveys are scheduled against anniversaries of the expiry date, so there is "
            "nothing to schedule against.");
        return false;
    }

    return true;
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

QString CertificateRepository::errorString() const
{
    return m_errorString;
}
