#pragma once

#include "modules/documents/domain/Document.h"

#include <QDialog>
#include <QString>

class DocumentVersionFields;
class QPushButton;

// Add New Version (document-management-spec.md §4.4): choose a file, with an
// optional revision label, date and change note. The new version becomes
// current and the previous one becomes superseded — which the repository
// decides, from the sequence, not this dialog.
class DocumentVersionDialog : public QDialog
{
    Q_OBJECT

public:
    // `documentTitle` is named in the window title, because a version is
    // always a version *of* something and the list behind the dialog may be
    // scrolled somewhere else by the time it opens.
    DocumentVersionDialog(const QString& documentTitle, QWidget* parent = nullptr);

    QString         sourcePath() const;
    DocumentVersion values() const;

private:
    DocumentVersionFields* m_fields   = nullptr;
    QPushButton*           m_okButton = nullptr;
};
