#include "modules/certificates/data/EndorsementRepository.h"

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
    if (endorsement.surveyType == SurveyType::Unset) {
        m_errorString = QStringLiteral("A survey type is required.");
        return false;
    }
    if (!endorsement.endorsementDate.isValid()) {
        m_errorString = QStringLiteral("An endorsement date is required.");
        return false;
    }

    // certificate-control-spec.md §4.8 item 5: a survey cannot have happened
    // before the certificate existed.
    if (certificateIssueDate.isValid() && endorsement.endorsementDate < certificateIssueDate) {
        m_errorString = QStringLiteral(
            "The endorsement date is before the certificate was issued on %1.")
                            .arg(certificateIssueDate.toString(QStringLiteral("dd MMM yyyy")));
        return false;
    }

    return true;
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

QString EndorsementRepository::errorString() const
{
    return m_errorString;
}
