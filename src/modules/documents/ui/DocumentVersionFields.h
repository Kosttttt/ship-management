#pragma once

#include "modules/documents/domain/Document.h"

#include <QString>
#include <QWidget>

class QDateEdit;
class QCheckBox;
class QLabel;
class QLineEdit;

// The four things a version is made of: the file, and the optional revision
// label, revision date and one-line change note
// (document-management-spec.md §4.1).
//
// One widget rather than the same four fields built twice, because Add
// Document and Add New Version need them identically — the same reason
// AttachmentSourceField exists in the certificates module.
class DocumentVersionFields : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentVersionFields(QWidget* parent = nullptr);

    // The file the user chose, absolute. Empty until one is chosen, which is
    // how both dialogs know they cannot be accepted yet.
    QString sourcePath() const;

    // The label, date and note as typed. The date is invalid unless the user
    // ticked it on: a date nobody entered must not be stored as today's
    // (§4.1 — it is information only, and a wrong one is worse than none).
    DocumentVersion values() const;

signals:
    // So a dialog's OK button can follow whether a file has been chosen.
    void fileChosen();

private:
    void chooseFile();

    QLineEdit* m_labelEdit = nullptr;
    QCheckBox* m_dateCheck = nullptr;
    QDateEdit* m_dateEdit  = nullptr;
    QLineEdit* m_noteEdit  = nullptr;
    QLabel*    m_fileLabel = nullptr;

    QString m_sourcePath;
};
