#include "modules/certificates/data/EndorsementRepository.h"

#include "core/InstallationContext.h"
#include "modules/certificates/domain/CertificateValidation.h"

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

QVariant dateToStorage(const QDate& date)
{
    // A calendar date, YYYY-MM-DD (CLAUDE.md §6.3).
    return date.isValid() ? QVariant(date.toString(Qt::ISODate))
                          : QVariant(QMetaType(QMetaType::QString));
}

QVariant nullableText(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

Endorsement endorsementFromRow(const QSqlQuery& query)
{
    Endorsement endorsement;
    endorsement.id              = query.value(0).toString();
    endorsement.certificateId   = query.value(1).toString();
    endorsement.surveyType      = SurveyTypeCodes::fromCode(query.value(2).toString());
    endorsement.endorsementDate = QDate::fromString(query.value(3).toString(), Qt::ISODate);
    endorsement.place           = query.value(4).toString();
    endorsement.surveyor        = query.value(5).toString();
    endorsement.result          = query.value(6).toString();
    endorsement.remarks         = query.value(7).toString();
    return endorsement;
}

} // namespace

EndorsementRepository::EndorsementRepository(QSqlDatabase&              database,
                                             const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
{
}

bool EndorsementRepository::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool EndorsementRepository::list(const QString& certificateId, QList<Endorsement>* endorsements)
{
    m_errorString.clear();
    endorsements->clear();

    // The join is what makes the scope filter possible at all: the vessel a
    // certificate belongs to is the only thing that says whether this
    // installation may see its endorsements.
    QString sql = QStringLiteral(
        "SELECT e.id, e.certificate_id, e.survey_type, e.endorsement_date,"
        "       e.place, e.surveyor, e.result, e.remarks"
        "  FROM endorsement e"
        "  JOIN certificate c ON c.id = e.certificate_id"
        " WHERE e.certificate_id = ? AND e.is_deleted = 0 AND c.is_deleted = 0");

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND c.vessel_id = ?");
    }
    sql += QStringLiteral(" ORDER BY e.endorsement_date");

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(certificateId);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the endorsements:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        endorsements->append(endorsementFromRow(query));
    }
    return true;
}

bool EndorsementRepository::findById(const QString&              id,
                                     std::optional<Endorsement>* endorsement)
{
    m_errorString.clear();
    endorsement->reset();

    if (id.isEmpty()) {
        return true; // nothing asked for, nothing found — not an error
    }

    QString sql = QStringLiteral(
        "SELECT e.id, e.certificate_id, e.survey_type, e.endorsement_date,"
        "       e.place, e.surveyor, e.result, e.remarks"
        "  FROM endorsement e"
        "  JOIN certificate c ON c.id = e.certificate_id"
        " WHERE e.id = ? AND e.is_deleted = 0 AND c.is_deleted = 0");

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND c.vessel_id = ?");
    }

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(id);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the endorsement:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        *endorsement = endorsementFromRow(query);
    }
    return true;
}

bool EndorsementRepository::loadParentCertificate(const QString& certificateId,
                                                  QString*       vesselId,
                                                  QDate*         issueDate)
{
    if (certificateId.isEmpty()) {
        m_errorString = QStringLiteral("An endorsement must belong to a certificate.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT vessel_id, issue_date FROM certificate WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(certificateId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the certificate:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (!query.next()) {
        m_errorString = QStringLiteral(
            "That certificate no longer exists, so an endorsement cannot be recorded against it.");
        return false;
    }

    *vesselId  = query.value(0).toString();
    *issueDate = QDate::fromString(query.value(1).toString(), Qt::ISODate);

    if (isScopedToOneVessel() && *vesselId != m_installation.vesselScope()) {
        m_errorString =
            QStringLiteral("This installation may only manage its own vessel's certificates.");
        return false;
    }

    return true;
}

bool EndorsementRepository::validate(const Endorsement& endorsement,
                                     const QDate&       certificateIssueDate)
{
    // In the domain layer for the same reason CertificateRepository::validate()
    // is: CSV import's preview has to know whether a survey date in the file
    // would be accepted before anything is written
    // (certificate-import-export-spec §6). This stays the write path's gate.
    return validateEndorsement(endorsement, certificateIssueDate, &m_errorString);
}

bool EndorsementRepository::create(const Endorsement& endorsement, QString* newId)
{
    m_errorString.clear();

    QString vesselId;
    QDate   issueDate;
    if (!loadParentCertificate(endorsement.certificateId, &vesselId, &issueDate)) {
        return false;
    }
    if (!validate(endorsement, issueDate)) {
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO endorsement"
        " (id, certificate_id, survey_type, endorsement_date, place, surveyor, result, remarks,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(endorsement.certificateId);
    query.addBindValue(SurveyTypeCodes::toCode(endorsement.surveyType));
    query.addBindValue(dateToStorage(endorsement.endorsementDate));
    query.addBindValue(nullableText(endorsement.place.trimmed()));
    query.addBindValue(nullableText(endorsement.surveyor.trimmed()));
    query.addBindValue(nullableText(endorsement.result.trimmed()));
    query.addBindValue(nullableText(endorsement.remarks.trimmed()));
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(m_installation.nodeId());

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the endorsement:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool EndorsementRepository::loadParentOfEndorsement(const QString& endorsementId,
                                                    QString*       certificateId,
                                                    QDate*         issueDate)
{
    if (endorsementId.isEmpty()) {
        m_errorString = QStringLiteral("No endorsement was given.");
        return false;
    }

    // The join is what makes the scope filter possible: an endorsement has no
    // vessel of its own, so the only thing that says whether this installation
    // may write to it is the vessel its certificate belongs to.
    QString sql = QStringLiteral(
        "SELECT c.id, c.issue_date, c.vessel_id"
        "  FROM endorsement e"
        "  JOIN certificate c ON c.id = e.certificate_id"
        " WHERE e.id = ? AND e.is_deleted = 0 AND c.is_deleted = 0");

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(endorsementId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the endorsement:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (!query.next()) {
        m_errorString = QStringLiteral("This endorsement is no longer in the database.");
        return false;
    }

    *certificateId = query.value(0).toString();
    *issueDate     = QDate::fromString(query.value(1).toString(), Qt::ISODate);

    if (isScopedToOneVessel() && query.value(2).toString() != m_installation.vesselScope()) {
        m_errorString =
            QStringLiteral("This installation may only manage its own vessel's certificates.");
        return false;
    }

    return true;
}

bool EndorsementRepository::update(const Endorsement& endorsement)
{
    m_errorString.clear();

    if (endorsement.id.isEmpty()) {
        m_errorString =
            QStringLiteral("This endorsement has no identifier, so it cannot be saved.");
        return false;
    }

    // Addressed by the endorsement's own id rather than by the certificate id
    // on the struct handed in: the row's real parent is what scope and the
    // "not before issue" check have to be measured against, and a struct
    // arriving with a different certificate id must not be able to move an
    // endorsement to another certificate.
    QString storedCertificateId;
    QDate   issueDate;
    if (!loadParentOfEndorsement(endorsement.id, &storedCertificateId, &issueDate)) {
        return false;
    }
    if (!validate(endorsement, issueDate)) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE endorsement SET"
        "  survey_type = ?, endorsement_date = ?, place = ?, surveyor = ?, result = ?,"
        "  remarks = ?, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(SurveyTypeCodes::toCode(endorsement.surveyType));
    query.addBindValue(dateToStorage(endorsement.endorsementDate));
    query.addBindValue(nullableText(endorsement.place.trimmed()));
    query.addBindValue(nullableText(endorsement.surveyor.trimmed()));
    query.addBindValue(nullableText(endorsement.result.trimmed()));
    query.addBindValue(nullableText(endorsement.remarks.trimmed()));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(endorsement.id);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the endorsement:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This endorsement no longer exists and could not be saved.");
        return false;
    }

    return true;
}

bool EndorsementRepository::softDelete(const QString& id)
{
    m_errorString.clear();

    // Scope-checked before the UPDATE, not left to the WHERE clause: another
    // vessel's endorsement must be refused with a message rather than quietly
    // affecting zero rows and reporting success.
    QString certificateId;
    QDate   issueDate;
    if (!loadParentOfEndorsement(id, &certificateId, &issueDate)) {
        return false;
    }

    // Soft delete, never a hard DELETE — CLAUDE.md §6.4.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE endorsement SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(id);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not delete the endorsement:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This endorsement is no longer in the database.");
        return false;
    }

    return true;
}

bool EndorsementRepository::deleteAllForCertificate(const QString& certificateId)
{
    m_errorString.clear();

    // Soft delete, never a hard DELETE — CLAUDE.md §6.4. A deletion has to
    // replicate to the other side, and the audit trail has to survive it.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE endorsement SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE certificate_id = ? AND is_deleted = 0"));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(certificateId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not clear the certificate's recorded surveys:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    return true; // affecting zero rows (nothing to clear) is not a failure
}

QString EndorsementRepository::errorString() const
{
    return m_errorString;
}
