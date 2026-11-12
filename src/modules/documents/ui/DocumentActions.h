#pragma once

#include "modules/documents/domain/Document.h"

#include <QObject>
#include <QString>

class DocumentFileStore;
class DocumentRepository;
class QPushButton;
class QTableWidget;

// The row data the documents table carries besides its visible text.
//
// In a header of its own because it is written in one place and read in
// another — DocumentListWidget fills the rows, DocumentActions reads them
// back — and a shared constant is what stops the writer and the reader
// drifting onto different numbers. The same reason CertificateListRow.h
// exists in the other module.
namespace DocumentRow {

constexpr int kDocumentIdRole = Qt::UserRole + 1;

// Whether this row may be changed here: false for a fleet-wide document on a
// vessel installation (document-management-spec.md §4.3). Carried on the row
// so the buttons do not have to ask the repository about the selection every
// time it moves.
constexpr int kEditableRole = Qt::UserRole + 2;

constexpr int kDataColumn = 0;

} // namespace DocumentRow

// The five selection-driven actions of the Documents screen
// (document-management-spec.md §4.4): Add New Version, Open, Versions, Edit
// Details and Delete.
//
// Its own class, built like CertificateListActions: it owns the buttons,
// watches the table's selection and asks for a reload when it has changed
// something. The screen only places the buttons.
class DocumentActions : public QObject
{
    Q_OBJECT

public:
    DocumentActions(DocumentRepository& repository, DocumentFileStore& files,
                    QTableWidget* table, QWidget* parent);

    QPushButton* addVersionButton() const;
    QPushButton* openButton() const;
    QPushButton* versionsButton() const;
    QPushButton* editButton() const;
    QPushButton* deleteButton() const;

    // Enables or disables the buttons for the current selection. Public
    // because a reload clears the selection without emitting a selection
    // change, so the screen has to say so.
    void updateButtonStates();

    // Opens the current version of whichever row was double-clicked.
    void openDocument(int row);

signals:
    // Something changed and the list is now stale.
    void listChanged();

private:
    // The selected row's document id, or empty when the selection is not one
    // row. Every action below starts here.
    QString selectedDocumentId() const;

    // The selected row's document, read through the repository so the scope
    // applies. False when nothing is selected or it is no longer there, the
    // message already shown.
    bool selectedDocument(Document* document);
    bool    selectionIsEditable() const;

    void addVersion();
    void openCurrent();
    void showVersions();
    void editDetails();
    void deleteDocument();

    // Where the files are, read fresh each time: Settings can change it while
    // the program runs, so a captured copy would go stale.
    QString documentsRoot() const;

    // Says so and points at Settings, rather than failing obscurely later.
    bool requireDocumentsFolder();

    DocumentRepository& m_repository;
    DocumentFileStore&  m_files;
    QTableWidget*       m_table       = nullptr;
    QWidget*            m_dialogParent = nullptr;

    QPushButton* m_addVersionButton = nullptr;
    QPushButton* m_openButton       = nullptr;
    QPushButton* m_versionsButton   = nullptr;
    QPushButton* m_editButton       = nullptr;
    QPushButton* m_deleteButton     = nullptr;
};
