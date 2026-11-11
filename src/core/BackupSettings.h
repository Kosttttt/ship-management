#pragma once

#include <QString>

// Where this computer keeps its backups, and whether the daily one is on
// (database-backup-spec §3).
//
// **QSettings, not the database.** The backup folder is a path on *this*
// computer. If it lived in `app_setting` it would travel inside a backup, and
// restoring on another machine would arrive pointing at a folder that does
// not exist there. The same reasoning TableHeaderSettings and the attachment
// chooser's last folder already follow.
//
// Free functions in a namespace rather than a class: there is no state to
// hold, and every value is read and written straight from QSettings so that
// two screens can never disagree about what is stored.
namespace BackupSettings {

// "Back up automatically once a day". Off until the developer turns it on,
// because an automatic backup with no folder chosen has nowhere to go.
bool automaticEnabled();
void setAutomaticEnabled(bool on);

// The folder the daily backup is written to — a Google Drive folder, a USB
// drive, anywhere. Empty when nothing has been chosen.
QString automaticFolder();
void    setAutomaticFolder(const QString& folder);

// Where the manual "Back up now…" dialog opens. The automatic folder is the
// sensible first suggestion, so this falls back to it, and then to Documents
// (database-backup-spec §3).
QString lastManualFolder();
void    setLastManualFolder(const QString& folder);

// Tests point this at a throwaway .ini file so they never read or write the
// developer's own settings (CLAUDE.md §10.1, database-backup-spec §7) — the
// same override TableHeaderSettings and AttachmentChooser have. An empty path
// clears it.
void setSettingsFileOverride(const QString& filePath);

} // namespace BackupSettings
