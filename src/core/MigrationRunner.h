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

    // Where the migrations compiled into this executable live. A path inside
    // the binary, not a folder on disk, so an installed copy cannot lose or
    // half-copy them and there is nothing to guess at runtime.
    //
    // Named here rather than spelled out at each call site: main() opens the
    // database with it, and the backup check needs the same answer
    // (database-backup-spec §5.1 check 3).
    static QString compiledInPath();

    // Applies whatever is outstanding. Returns false and sets errorString()
    // on the first failure, leaving the failed migration fully rolled back.
    bool run();

    // The highest migration number this program carries, for the backup
    // check that refuses a backup made by a newer version
    // (database-backup-spec §5.1 check 3). Reads the migrations folder, not
    // the database, and touches nothing: this is "what do I know how to do",
    // not "what has been done here".
    bool highestAvailableVersion(int* version);

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
