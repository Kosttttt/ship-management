#pragma once

#include "core/MigrationRunner.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

// Shared fixture for the four backup and restore suites
// (database-backup-spec §7). Header-only, so each suite gets the same
// helpers without any of them owning it, and none grows past the size limit
// in CLAUDE.md §9.
//
// Everything here works on *files*, inside a QTemporaryDir the calling suite
// owns. Nothing touches the real data folder or the real settings
// (CLAUDE.md §10.1).
namespace BackupTestSupport {

// Opens a file-backed database on its own connection and applies the real
// migrations, so what is backed up and inspected is the schema the
// application actually runs.
inline bool openFileDatabase(const QString& connectionName, const QString& filePath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(filePath);
    if (!db.open()) {
        qWarning() << "could not open" << filePath << ":" << db.lastError().text();
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

// The one installation row every complete database has. Written directly
// rather than through InstallationRepository, so a suite can also make the
// malformed rows §5.1 has to refuse.
inline bool seedInstallation(QSqlDatabase& db, const QString& mode, const QString& nodeId,
                             const QString& vesselId  = QString(),
                             const QString& rowId     = QStringLiteral("install-1"),
                             bool           singleton = true)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO installation (id, singleton, installation_mode, node_id, vessel_id,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
        " 0, ?, 1)"));
    query.addBindValue(rowId);
    query.addBindValue(singleton ? 1 : 2);
    query.addBindValue(mode);
    query.addBindValue(nodeId);
    query.addBindValue(vesselId.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                          : QVariant(vesselId));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        qWarning() << "installation seed failed:" << query.lastError().text();
        return false;
    }
    return true;
}

inline QString seedVessel(QSqlDatabase& db, const QString& name, const QString& imoNumber,
                          const QString& updatedAt = QStringLiteral("2026-01-01T00:00:00Z"))
{
    const QString id = QStringLiteral("vessel-%1").arg(imoNumber);

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
        " updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', ?, 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(imoNumber);
    query.addBindValue(updatedAt);

    if (!query.exec()) {
        qWarning() << "vessel seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

inline bool seedCertificate(QSqlDatabase& db, const QString& vesselId, const QString& name,
                            const QString& updatedAt = QStringLiteral("2026-01-01T00:00:00Z"))
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, 'STATUTORY', '2026-02-04', '2031-02-04',"
        " '2026-01-01T00:00:00Z', 'SEED', ?, 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(QStringLiteral("cert-%1-%2").arg(vesselId, name));
    query.addBindValue(vesselId);
    query.addBindValue(name);
    query.addBindValue(updatedAt);

    if (!query.exec()) {
        qWarning() << "certificate seed failed:" << query.lastError().text();
        return false;
    }
    return true;
}

// A complete OFFICE database at `filePath`, with one vessel and
// `certificateCount` certificates, closed again afterwards so the file can be
// inspected, copied or renamed. Returns false on any failure.
inline bool makeDatabaseFile(const QString& filePath, const QString& nodeId,
                             int certificateCount = 1)
{
    const QString connectionName =
        QStringLiteral("backup_support_%1").arg(QFileInfo(filePath).fileName());

    if (!openFileDatabase(connectionName, filePath)) {
        return false;
    }

    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        ok              = seedInstallation(db, QStringLiteral("OFFICE"), nodeId);
        if (ok) {
            const QString vesselId =
                seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
            ok = !vesselId.isEmpty();
            for (int index = 0; ok && index < certificateCount; ++index) {
                ok = seedCertificate(db, vesselId, QStringLiteral("Certificate %1").arg(index));
            }
        }
    }

    closeDatabase(connectionName);
    return ok;
}

// Reads back the single scalar a test wants to compare across a backup.
inline QVariant scalarFrom(const QString& filePath, const QString& sql)
{
    const QString connectionName = QStringLiteral("backup_read_%1")
                                       .arg(QFileInfo(filePath).fileName());

    QVariant value;
    if (openFileDatabase(connectionName, filePath)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        QSqlQuery    query(db);
        if (query.exec(sql) && query.next()) {
            value = query.value(0);
        }
    }
    closeDatabase(connectionName);
    return value;
}

// An automatic backup file for a node and date, with nothing inside it. The
// pruning and "already done today" rules read names, never contents, so a
// placeholder is exactly the right thing to test them with.
inline bool touchFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write("not a database");
    return true;
}

} // namespace BackupTestSupport
