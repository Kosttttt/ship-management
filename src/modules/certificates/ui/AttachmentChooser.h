#pragma once

#include <QString>

class QWidget;

// The two "where is the document?" prompts from certificate-attachment-spec §8,
// in one place because three screens offer exactly the same pair: the
// certificate list, the renewal dialog, and the extension dialog.
namespace AttachmentChooser {

// Both return an empty string when the user cancels, which every caller treats
// as "change nothing".
//
// No file-type filter on either: §3 declines file-type validation outright —
// whatever the crew already has is what they will attach.
//
// vesselAttachmentRoot is the current vessel's configured attachment folder,
// or empty when it has none. It is passed in rather than looked up because
// this is a widget-layer helper with no repositories of its own; the callers
// already have the service that knows. It is used for one thing: making sure
// a remembered folder inside the managed folder is never reopened
// (housekeeping-round-spec §5).
QString chooseFile(QWidget* parent, const QString& vesselAttachmentRoot);
QString chooseFolder(QWidget* parent, const QString& vesselAttachmentRoot);

// Where the next dialog will open. Public so the decision can be tested on
// its own: a modal file dialog cannot be driven from a test, but this is the
// whole of what the round changed (§5).
//
// The last folder a document was picked from, when it still exists and is not
// the vessel's own attachment folder or inside it. Documents otherwise.
QString startingFolder(const QString& vesselAttachmentRoot);

// What a successful pick remembers: for a file the folder holding it, and for
// a folder of documents that folder's parent, so next time the folder just
// chosen is visible among its neighbours. One remembered folder, shared by
// both dialogs.
//
// An empty path remembers nothing, which is what makes Cancel harmless.
void rememberChosenFile(const QString& filePath);
void rememberChosenFolder(const QString& folderPath);

// Tests point this at a throwaway .ini file so they never write into the
// developer's own settings — the same override TableHeaderSettings has, for
// the same reason (CLAUDE.md §10.1). An empty path clears it.
void setSettingsFileOverride(const QString& filePath);

} // namespace AttachmentChooser
