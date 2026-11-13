#pragma once

#include "modules/documents/domain/DocumentSpace.h"

#include <QString>
#include <QWidget>

class DocumentPageRemovals;
class DocumentSpaceRepository;
class QListWidget;
class QPushButton;

// The spaces list, top left (document-management-spec.md §5): company spaces
// first, then vessel spaces, with New Space…, Rename and Delete.
//
// Its own widget so the screen that puts the three panels together stays
// something a person can read (CLAUDE.md §9). It asks the repository what may
// be done and greys its own buttons accordingly; the repository refuses
// regardless, which is where the rule actually lives (§4.3).
class DocumentSpacePanel : public QWidget
{
    Q_OBJECT

public:
    DocumentSpacePanel(DocumentSpaceRepository& spaces, DocumentPageRemovals& removals,
                       QWidget* parent = nullptr);

    // Re-reads the list, keeping the selection by id where it still exists —
    // by id rather than by row, because adding a space shifts the rows.
    void reload();

    // The selected space, or an empty one when nothing is selected.
    DocumentSpace selectedSpace() const;

signals:
    // The selection changed, or the list was rebuilt under it.
    void spaceSelected(const QString& spaceId);

private:
    void createSpace();
    void renameSpace();
    void deleteSpace();
    void updateButtons();

    DocumentSpaceRepository& m_spaces;
    // Deleting a space takes its pages with it, which is a removal.
    DocumentPageRemovals&    m_removals;

    QListWidget* m_list          = nullptr;
    QPushButton* m_newButton     = nullptr;
    QPushButton* m_renameButton  = nullptr;
    QPushButton* m_deleteButton  = nullptr;

    QList<DocumentSpace> m_loaded;
};
