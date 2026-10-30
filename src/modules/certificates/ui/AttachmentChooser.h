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
QString chooseFile(QWidget* parent);
QString chooseFolder(QWidget* parent);

} // namespace AttachmentChooser
