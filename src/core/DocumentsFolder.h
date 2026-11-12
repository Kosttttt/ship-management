#pragma once

#include <QString>

// Where this installation keeps its document files
// (document-management-spec.md §4.2).
//
// **QSettings, not the database**, for the same reason the backup folder and
// each table's column widths are: it is a path on *this* computer. Stored in
// the database it would travel inside a backup and arrive on another machine
// pointing at a folder that does not exist there — so after a restore the
// folder is chosen again, exactly as each vessel's attachment folder is.
//
// In core/ rather than in the documents module because it is the same kind of
// per-machine setting the rest of core already owns, and because the Settings
// page that offers it lives in app/, which must not reach into a module's
// folders (CLAUDE.md §5).
//
// Free functions in a namespace: there is no state to hold, and every value
// is read and written straight from QSettings so two screens can never
// disagree about what is stored.
namespace DocumentsFolder {

// The chosen folder, or empty when none has been chosen yet. Empty is a
// normal state, not an error: the program runs without one, and the screens
// that need it say so and point at Settings.
QString path();
void    setPath(const QString& absolutePath);

// Tests point this at a throwaway .ini file so they never read or write the
// developer's own settings (CLAUDE.md §10.1) — the same override
// TableHeaderSettings, AttachmentChooser and BackupSettings have. An empty
// path clears it.
void setSettingsFileOverride(const QString& filePath);

} // namespace DocumentsFolder
