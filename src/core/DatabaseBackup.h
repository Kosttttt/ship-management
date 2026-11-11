#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

class QSqlDatabase;

// Making a backup of the live database (database-backup-spec §2).
//
// Through the program's own open connection, with SQLite's `VACUUM INTO`,
// which writes a complete and consistent copy while the program keeps
// running. Not a file copy: the one rule this whole feature follows is that
// the program never guesses where its database is (§1, and CLAUDE.md §10.1,
// which exists because a path guessed from outside the app destroyed a real
// database).
namespace DatabaseBackup {

// §2's names, so a folder of backups reads sensibly and two computers can
// share one Google Drive folder without clashing:
//   manual:    ship-management-<node>-<yyyy-MM-dd_HHmm>.db
//   automatic: ship-management-auto-<node>-<yyyy-MM-dd>.db
//
// <node> is the installation's node id — 'OFFICE' or 'VESSEL-<IMO>'.
QString manualFileName(const QString& nodeId, const QDateTime& when);
QString automaticFileName(const QString& nodeId, const QDate& when);

// True when a name is one of this node's *automatic* backups. What the daily
// backup uses to decide whether today's is already done, and the only thing
// pruning is ever allowed to delete (§4).
bool isAutomaticFileName(const QString& fileName, const QString& nodeId);

// The date in one of this node's automatic backup names, or an invalid QDate
// when the name is not one.
QDate dateOfAutomaticFileName(const QString& fileName, const QString& nodeId);

// Writes a checked backup to targetFilePath.
//
// The folder must already exist — it is deliberately not created, so that a
// Google Drive folder that is not there, or an unplugged USB drive, is
// reported rather than quietly recreated somewhere useless (§4).
//
// Written as "<target>.tmp" and renamed only once it is complete and has
// passed PRAGMA integrity_check, so a sync client never sees a half-written
// file and a backup that failed its check is never left looking good. A
// failed check deletes our own temporary file and nothing else.
bool writeTo(QSqlDatabase& connection, const QString& targetFilePath, QString* errorString);

} // namespace DatabaseBackup
