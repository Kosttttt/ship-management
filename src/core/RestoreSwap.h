#pragma once

#include <QString>
#include <QStringList>

// Replacing the live database with a backup (database-backup-spec §5.3).
//
// Not during a session: repositories and screens all hold the open
// connection, so the file cannot safely be swapped underneath them. Instead
// the chosen backup is copied in beside the live database under a pending
// name, the program restarts, and the swap happens at the next start before
// anything opens the database.
//
// Every function takes the data folder as a parameter (§6). Only main()
// passes the real one — from Database::defaultFilePath(), the same call that
// opens the database, so it is the right folder by construction rather than
// by a guess (§1, and CLAUDE.md §10.1).
//
// **Nothing here ever deletes a database.** The file being replaced is
// renamed to a `before-restore-…` safety copy beside it, and those are not
// cleaned up automatically.
namespace RestoreSwap {

QString liveFileName();
QString pendingFileName();

QString liveFilePath(const QString& dataFolder);
QString pendingFilePath(const QString& dataFolder);

// §5.3 step 1: copy an already-checked backup into the data folder under the
// pending name, written to a temporary name and renamed so that a half-copied
// file is never mistaken for a pending restore.
//
// The caller must have run BackupFileInspector::inspect() first. This does
// not check the file again; it is the step that happens once the user has
// said yes.
bool stage(const QString& dataFolder, const QString& backupFilePath, QString* errorString);

enum class Outcome {
    NothingPending,  // the ordinary start: no restore was asked for
    Restored,
    Failed
};

// §5.3 step 3, to be called at startup **before** the database is opened.
//
// Renames the live database — and any -journal, -wal or -shm beside it, which
// belong to it and would corrupt a different database if left behind — to
// `ship-management.before-restore-<yyyy-MM-dd_HHmmss>.db`, then renames the
// pending file into place.
//
// If that second rename fails, the safety copy is put back and the original
// database is left ready to open: the folder is never left without one.
//
// `detail` is a sentence for the log and, on failure, for the user. On
// Restored it names the safety copy.
Outcome applyPending(const QString& dataFolder, QString* detail);

} // namespace RestoreSwap
