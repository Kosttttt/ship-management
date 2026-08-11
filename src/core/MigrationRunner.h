#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QSqlDatabase;

// Brings a database up to date by applying the numbered .sql files in a
// migrations folder that have not been applied yet, in numeric order.
//
// CLAUDE.md §6.6: a committed migration is never edited, because installed
// copies have already run it. This class enforces that rule rather than merely
// documenting it — it stores a checksum of every migration it applies and
// refuses to continue if the file has changed since.
//
// The folder is a constructor argument, not a hard-coded path, so the unit
// tests can point it at a temporary directory of their own making.
class MigrationRunner
{
public:
    MigrationRunner(QSqlDatabase& database, const QString& migrationsDirectory);

    // Applies whatever is outstanding. Returns false and sets errorString()
    // on the first failure, leaving the failed migration fully rolled back.
    bool run();

    QString     errorString() const;
    QStringList appliedInLastRun() const;

private:
    struct MigrationFile {
        int     version = 0;
        QString fileName;
        QString filePath;
    };

    bool ensureSchemaVersionTable();
    bool discoverMigrations(QList<MigrationFile>* files);
    bool readRecordedChecksums(QHash<int, QString>* checksums);
    bool readMigrationFile(const MigrationFile& file, QString* sql);
    bool applyMigration(const MigrationFile& file, const QString& sql, const QString& checksum);
    bool recordMigration(const MigrationFile& file, const QString& checksum);

    QSqlDatabase& m_database;
    QString       m_migrationsDirectory;
    QString       m_errorString;
    QStringList   m_applied;
};
