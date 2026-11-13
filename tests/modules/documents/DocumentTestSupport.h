#pragma once

#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

// Shared fixture for the Document Management suites
// (document-management-spec.md §7). Header-only, so each suite gets the same
// helpers without any of them owning it.
//
// Every database here is in memory. Nothing can reach the real one
// (CLAUDE.md §10.1).
namespace DocumentTestSupport {

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

// The same, but stopping before a given migration — so a suite can set a
// database up as it was at version N, put rows in it and then run the rest.
// Used by the drop-migration test, which has to start from a database that
// still has 012's tables (§7).
inline bool openDatabaseAtVersion(const QString& connectionName, int highestVersion)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    if (!db.open()) {
        return false;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    // The real migration files, copied into a folder holding only the ones
    // up to `highestVersion`. Real files rather than hand-written SQL,
    // because the point is to exercise what will actually run on the
    // developer's machine.
    static QTemporaryDir folders[16];
    QTemporaryDir&       folder = folders[qBound(0, highestVersion, 15)];
    if (!folder.isValid()) {
        return false;
    }

    const QDir source(QStringLiteral(":/migrations"));
    for (const QString& name : source.entryList({QStringLiteral("*.sql")}, QDir::Files)) {
        if (name.left(3).toInt() > highestVersion) {
            continue;
        }
        const QString target = folder.filePath(name);
        if (!QFile::exists(target) && !QFile::copy(source.filePath(name), target)) {
            return false;
        }
    }

    MigrationRunner runner(db, folder.path());
    if (!runner.run()) {
        qWarning() << "partial migrations failed:" << runner.errorString();
        return false;
    }
    return true;
}

// Runs whatever migrations are outstanding, which is what a program start
// does after an update.
inline bool runRemainingMigrations(QSqlDatabase& db, QString* error)
{
    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    if (!runner.run()) {
        *error = runner.errorString();
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

inline QString seedVessel(QSqlDatabase& db, const QString& id, const QString& name,
                          const QString& imoNumber, bool archived = false)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
        " updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
        " ?, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(imoNumber);
    // Archived is the same is_deleted flag vessel archive/restore uses, which
    // is what makes "an archived vessel's space is hidden" testable.
    query.addBindValue(archived ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "vessel seed failed:" << query.lastError().text();
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

inline bool tableExists(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?"));
    query.addBindValue(tableName);
    return query.exec() && query.next() && query.value(0).toInt() == 1;
}

} // namespace DocumentTestSupport
