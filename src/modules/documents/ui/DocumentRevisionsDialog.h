#pragma once

#include "modules/documents/domain/DocumentSpace.h"

#include <QDialog>
#include <QString>

class DocumentHistoryReader;
class DocumentImageStore;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// A manual's Record of Revisions (document-history-spec.md §3): Rev 1, Rev 2,
// … newest first, each with its date and note, and under it the pages it made
// new, changed or withdrew.
//
// This is the screen form of the Record of Revisions that step 5 will print —
// same data, in the same order, from the same reader. The printed one is a
// page of a manual that a surveyor reads; this one is the same answer on
// screen, which is why they must not be worked out twice.
//
// Read-only throughout. A page listed here opens as it was published, and a
// changed page offers the comparison with the revision before it.
class DocumentRevisionsDialog : public QDialog
{
    Q_OBJECT

public:
    DocumentRevisionsDialog(DocumentHistoryReader& history, DocumentImageStore& images,
                            QWidget* parent = nullptr);

    // Fills the list for one Manual space. False when it could not be read,
    // with the reason in errorString().
    bool showFor(const DocumentSpace& space);

    QString errorString() const { return m_errorString; }

private:
    void updateButtons();
    void openSelected();
    void compareSelected();

    // The selected page's revision, or an empty string when a Rev row — or
    // nothing — is selected.
    QString selectedPageRevisionId() const;
    bool    selectedIsChanged() const;

    DocumentHistoryReader& m_history;
    DocumentImageStore&    m_images;

    QTreeWidget* m_tree          = nullptr;
    QPushButton* m_openButton    = nullptr;
    QPushButton* m_compareButton = nullptr;

    DocumentSpace m_space;
    QString       m_errorString;
};
