#pragma once

#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "core/Vessel.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

// Shared fixture for the VesselRepository test suites. Header-only, so both
// suites get the same helpers without either owning them, and neither file
// grows past the size limit in CLAUDE.md §9.
namespace VesselTestSupport {

// A private in-memory database with the real migrations applied, so the
// constraints under test are the ones the application runs against.
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

// Writes a row directly, bypassing the repository — used to simulate rows a
// future sync might bring in, which the scope filter must still hide.
inline QString seedVessel(QSqlDatabase& db, const QString& name, const QString& imoNumber)
{
    const QString id = QStringLiteral("id-%1").arg(imoNumber);

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
        " updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
        " 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(imoNumber);

    if (!query.exec()) {
        qWarning() << "seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

inline void markDeleted(QSqlDatabase& db, const QString& id)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE vessel SET is_deleted = 1 WHERE id = ?"));
    query.addBindValue(id);
    query.exec();
}

inline int rowCountOf(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

inline InstallationContext officeContext()
{
    InstallationRecord record;
    record.id     = QStringLiteral("install-office");
    record.mode   = InstallationMode::Office;
    record.nodeId = QStringLiteral("OFFICE");
    return InstallationContext(record);
}

inline InstallationContext vesselContext(const QString& vesselId, const QString& imoNumber)
{
    InstallationRecord record;
    record.id              = QStringLiteral("install-vessel");
    record.mode            = InstallationMode::Vessel;
    record.nodeId          = QStringLiteral("VESSEL-") + imoNumber;
    record.vesselId        = vesselId;
    record.vesselImoNumber = imoNumber;
    return InstallationContext(record);
}

inline Vessel sampleVessel(const QString& name, const QString& imoNumber)
{
    Vessel vessel;
    vessel.name           = name;
    vessel.imoNumber      = imoNumber;
    vessel.callSign       = QStringLiteral("ABCD");
    vessel.grossTonnage   = 51000;
    vessel.portOfRegistry = QStringLiteral("Limassol");
    vessel.flagState      = QStringLiteral("Cyprus");
    return vessel;
}

} // namespace VesselTestSupport
