#pragma once

#include <QWidget>

class QLabel;

// The "Documents folder" row on the Settings page
// (document-management-spec.md §4.5).
//
// Its own small widget, the way AttachmentFolderSettings is, which keeps
// SettingsPage under the ~300-line guideline (CLAUDE.md §9) and puts
// everything about this one setting in one place.
//
// In app/ rather than in the documents module because the Settings page is
// core-owned UI, and app/ reaching into a module's folders is what
// document-management-spec §3.1 is trying to stop growing. The setting it
// edits lives in core/DocumentsFolder for the same reason.
//
// Changing the folder does **not** move the files already in the old one —
// step 1 says so out loud rather than pretending otherwise (§4.5), and the
// widget tells the user when they change it.
class DocumentsFolderSettings : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentsFolderSettings(QWidget* parent = nullptr);

    // Re-reads the stored folder. Called when the Settings page is shown.
    void reload();

private:
    void chooseFolder();

    QLabel* m_folderLabel = nullptr;
};
