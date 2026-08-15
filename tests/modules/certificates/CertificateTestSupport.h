#pragma once

#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "modules/certificates/domain/Certificate.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

// Shared fixture for the certificate test suites, mirroring
// tests/core/VesselTestSupport.h.
namespace CertificateTestSupport {

inline bool openDatabase(const QString& connectionName)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    if (!db.open()) {
        qWarning() << "could not open in-memory database";
        return false;
    }

    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        return false;
    }

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    if (!runner.run()) {
        qWarning() << "migrations failed:" << runner.errorString();
        return false;
    }
    return true;
}

inline void closeDatabase(const QString& connectionName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
}

inline InstallationContext officeContext()
{
    InstallationRecord record;
    record.id     = QStringLiteral("install-office");
    record.mode   = InstallationMode::Office;
    record.nodeId = QStringLiteral("OFFICE");
    return InstallationContext(record);
}

inline InstallationContext vesselContext(const QString& vesselId)
{
    InstallationRecord record;
    record.id              = QStringLiteral("install-vessel");
    record.mode            = InstallationMode::Vessel;
    record.nodeId          = QStringLiteral("VESSEL-9074729");
    record.vesselId        = vesselId;
    record.vesselImoNumber = QStringLiteral("9074729");
    return InstallationContext(record);
}

// A valid certificate, ready to have one field spoiled by a test.
inline Certificate sampleCertificate(const QString& vesselId, const QString& name)
{
    Certificate certificate;
    certificate.vesselId          = vesselId;
    certificate.name              = name;
    certificate.category          = CertificateCategory::Statutory;
    certificate.certificateNumber = QStringLiteral("SC-12345");
    certificate.issueDate         = QDate(2026, 2, 4);
    certificate.expiryDate        = QDate(2031, 2, 4);
    certificate.issuedBy          = QStringLiteral("Lloyd's Register");
    certificate.placeOfIssue      = QStringLiteral("Limassol");
    certificate.requiresAnnualSurvey = true;
    return certificate;
}

// Writes a certificate row directly, bypassing the repository — used to
// simulate rows a future sync might bring in, which the scope filter must
// still hide.
inline QString seedCertificate(QSqlDatabase&  db,
                               const QString& id,
                               const QString& vesselId,
                               const QString& name,
                               bool           deleted    = false,
                               const QString& listNumber = QString())
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " list_number,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, 'STATUTORY', '2026-02-04', '2031-02-04', ?,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', ?, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(vesselId);
    query.addBindValue(name);
    query.addBindValue(listNumber.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                            : QVariant(listNumber));
    query.addBindValue(deleted ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

// Seeds a certificate with dates and survey rules chosen by the caller, for
// the tests that need a specific computed status rather than a generic row.
// An invalid expiryDate seeds SQL NULL — "never expires".
inline QString seedCertificateWithSchedule(QSqlDatabase&  db,
                                           const QString& id,
                                           const QString& vesselId,
                                           const QString& name,
                                           const QString& listNumber,
                                           const QDate&   issueDate,
                                           const QDate&   expiryDate,
                                           bool           requiresAnnual,
                                           bool           requiresIntermediate = false)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " list_number, requires_annual_survey, requires_intermediate_survey,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, 'STATUTORY', ?, ?, ?, ?, ?,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(vesselId);
    query.addBindValue(name);
    query.addBindValue(issueDate.toString(Qt::ISODate));
    query.addBindValue(expiryDate.isValid() ? QVariant(expiryDate.toString(Qt::ISODate))
                                            : QVariant(QMetaType(QMetaType::QString)));
    query.addBindValue(listNumber.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                            : QVariant(listNumber));
    query.addBindValue(requiresAnnual ? 1 : 0);
    query.addBindValue(requiresIntermediate ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

inline QString seedEndorsement(QSqlDatabase&  db,
                               const QString& id,
                               const QString& certificateId,
                               const QString& surveyTypeCode,
                               const QDate&   date)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO endorsement (id, certificate_id, survey_type, endorsement_date,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(certificateId);
    query.addBindValue(surveyTypeCode);
    query.addBindValue(date.toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "endorsement seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

inline int rowCountOf(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

} // namespace CertificateTestSupport
