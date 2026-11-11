#pragma once

#include <QDateTime>
#include <QString>

class QSqlDatabase;

// What a database holds, in the few numbers a person needs to decide whether
// to restore it (database-backup-spec §5.2).
struct BackupSummary {
    QString nodeId;              // 'OFFICE' or 'VESSEL-<IMO>'
    bool    isVesselMode = false;
    QString vesselName;          // the installation's own vessel; empty at the office

    int vesselCount      = 0;
    int certificateCount = 0;

    // The latest updated_at across the fleet and its certificates, as stored —
    // ISO-8601 UTC (CLAUDE.md §6.2). Invalid when there is nothing yet.
    //
    // Only those two tables, both of which §5.1 already requires to exist, so
    // that an older backup missing a table added later still summarises
    // instead of failing.
    QDateTime lastChangeUtc;

    int schemaVersion = 0;       // the highest applied migration

    QString modeLabel() const;   // "OFFICE" / "VESSEL", for the §5.2 table
};

// Checking a backup file before anything is written (database-backup-spec
// §5.1), and describing a database for the confirmation table (§5.2).
//
// Everything here opens its own read-only connection on a name of its own, so
// the live database is never touched and nothing can be written by accident.
// Free functions: there is no state worth holding between calls.
namespace BackupFileInspector {

// PRAGMA integrity_check on its own read-only connection. Also what
// DatabaseBackup uses to check what it has just written, which is why it is
// separate from inspect() below.
//
// A file that is not a database at all fails here: SQLite opens lazily, so the
// first real query is where that shows up.
bool passesIntegrityCheck(const QString& filePath, QString* errorString);

// The §5.2 numbers, read from an already-open connection — the "This computer
// now" column. inspect() calls this too, so both columns of that table come
// from one set of queries and cannot disagree.
bool summarise(QSqlDatabase& connection, BackupSummary* summary, QString* errorString);

// Every §5.1 check, in order, against a file:
//  1. it is a SQLite database and passes integrity_check;
//  2. it has this program's tables and exactly one installation row;
//  3. its schema is no newer than this program's.
//
// highestKnownMigration is what MigrationRunner::highestAvailableVersion()
// reports. An *older* backup is accepted — the usual migrations bring it up to
// date on the next start.
//
// errorString says which check failed, in words a person can act on.
bool inspect(const QString& filePath, int highestKnownMigration, BackupSummary* summary,
             QString* errorString);

} // namespace BackupFileInspector
