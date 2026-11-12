#pragma once

#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "modules/documents/domain/Document.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

// Shared fixture for the Document Management suites
// (document-management-spec.md §4.6). Header-only, so each suite gets the
// same helpers without any of them owning it.
//
// Everything works on an in-memory database and, where files are involved, a
// QTemporaryDir the calling suite owns. Nothing here can reach the real data
// folder, the real documents folder or the real settings (CLAUDE.md §10.1).
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
                          const QString& imoNumber)
{
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
        qWarning() << "vessel seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

// A document as the dialogs would hand it over. An empty vesselId is
// fleet-wide, which is the whole distinction §4.3 turns on.
inline Document sampleDocument(const QString& vesselId, const QString& reference,
                               const QString& title,
                               DocumentCategory category = DocumentCategory::Sms)
{
    Document document;
    document.vesselId  = vesselId;
    document.category  = category;
    document.reference = reference;
    document.title     = title;
    return document;
}

// A version with a stored path already worked out — what the repository is
// handed once the file has been copied in. The suites that care about real
// files use DocumentFileStore instead.
inline DocumentVersion sampleVersion(const QString& relativePath,
                                     const QString& originalName = QStringLiteral("source.pdf"))
{
    DocumentVersion version;
    version.filePath         = relativePath;
    version.originalFileName = originalName;
    return version;
}

// Writes a real file at an absolute path, creating its folders, so a test can
// check that a version's file is still there after something was deleted.
inline bool writeFile(const QString& absolutePath, const QByteArray& contents)
{
    const QFileInfo info(absolutePath);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(contents) == contents.size();
}

inline int rowCountOf(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

} // namespace DocumentTestSupport
