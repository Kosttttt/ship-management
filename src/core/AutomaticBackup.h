#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class QSqlDatabase;

// The daily backup that runs at startup (database-backup-spec §4).
//
// Every function takes the folder as a parameter and reads no settings of its
// own (§6), so the tests run entirely inside a QTemporaryDir and main() is
// the only caller that knows where the real folder is.
namespace AutomaticBackup {

// §4: the ten newest are kept.
constexpr int kKeepNewest = 10;

// The dates of this node's automatic backups already in the folder, newest
// first. A folder that is not there gives an empty list rather than an error:
// "nothing backed up yet" is the right reading of a Google Drive folder that
// is not mounted, and §4 says a missing folder must never stop startup.
QList<QDate> existingDates(const QString& folder, const QString& nodeId);

// Whether today's backup still needs making.
//
// Decided from the files in the folder rather than from a stored date, so a
// folder that was emptied, replaced or moved simply gets a fresh backup (§4).
bool isDueToday(const QString& folder, const QString& nodeId, QDate today);

// When the newest automatic backup for this node was written, for the
// Settings line (§3). Invalid when there is none.
//
// The file's own timestamp, not the date in its name: the name carries only a
// day, and a person reading "Last automatic backup" wants the time too.
QDateTime lastBackupTime(const QString& folder, const QString& nodeId);

// Deletes this node's automatic backups beyond the newest `keep`, oldest
// first. Returns how many were removed and, if `removed` is given, their
// names.
//
// It only ever deletes files matching its own automatic pattern for its own
// node id: never a manual backup, never another computer's backups, never
// anything else in the folder (§4). That is the whole reason this is a
// function of its own rather than three lines inside runIfDue().
int prune(const QString& folder, const QString& nodeId, int keep, QStringList* removed = nullptr);

enum class Outcome {
    NoFolder,          // nothing chosen, or it is not there right now
    AlreadyDoneToday,  // this node already has today's backup in that folder
    Saved,
    Failed
};

// The whole startup step: decide, back up, prune. Whether the daily backup is
// switched on at all is the caller's question — BackupSettings answers it —
// so that nothing here reads a setting.
//
// Never throws and never blocks: every outcome is a value, and §4 requires
// that none of them stops the program starting.
Outcome runIfDue(QSqlDatabase& connection, const QString& folder, const QString& nodeId,
                 QDate today, QString* savedPath, QString* errorString);

} // namespace AutomaticBackup
