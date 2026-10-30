#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

// The optional "attach a document" control that certificate-attachment-spec §8
// folds into two dialogs: the renewal dialog, where it replaces the
// certificate's own document, and the extension dialog, where it supplies the
// short-term certificate's own paperwork.
//
// One widget rather than the same label-plus-menu built twice, because the two
// dialogs need identical behaviour: a file or a folder may be chosen, the
// choice is only remembered — nothing is copied until the dialog is accepted —
// and leaving it alone must change nothing at all.
class AttachmentSourceField : public QWidget
{
    Q_OBJECT

public:
    // emptyText is what the field reads before anything is chosen, which
    // differs between the two dialogs: one says the current document stays,
    // the other that there is none.
    explicit AttachmentSourceField(const QString& emptyText, QWidget* parent = nullptr);

    // Empty when the user has chosen nothing, which every caller treats as
    // "do not touch the attachment".
    QString sourcePath() const;

    // Which of the two entries was used. Kept rather than worked out from the
    // path, because it is also what decides whether the stored copy keeps a
    // file extension (certificate-attachment-spec §6).
    bool sourceIsFolder() const;

private:
    void choose(bool folder);

    QLabel*      m_label  = nullptr;
    QPushButton* m_button = nullptr;

    QString m_emptyText;
    QString m_sourcePath;
    bool    m_sourceIsFolder = false;
};
