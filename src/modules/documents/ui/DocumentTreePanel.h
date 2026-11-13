#pragma once

#include "modules/documents/domain/DocumentPage.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QHash>
#include <QString>
#include <QWidget>

#include <memory>

class DocumentPageMoves;
class DocumentPageRemovals;
class DocumentPageRepository;
class DocumentPublisher;
class DocumentRevisionStore;
class DocumentTreeActions;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// The page tree, bottom left (document-management-spec.md §5): New Page,
// Move up / Move down, Move to…, Delete…, and — for a Manual space — Publish
// revision…
//
// A page with unpublished changes shows a marker; a page marked for
// withdrawal is struck through (§6.3). Both are read from the page row and
// its revisions, never from a stored flag (CLAUDE.md §6.7).
class DocumentTreePanel : public QWidget
{
    Q_OBJECT

public:
    DocumentTreePanel(DocumentPageRepository& pages, DocumentPageMoves& moves,
                      DocumentPageRemovals& removals, DocumentRevisionStore& revisions,
                      DocumentPublisher& publisher, QWidget* parent = nullptr);

    // Out of line because m_actions holds a type this header only names.
    ~DocumentTreePanel() override;

    // Which space the tree is showing. An empty space clears it.
    void setSpace(const DocumentSpace& space, bool editable);

    // Re-reads the tree, keeping the selection by page id.
    void reload();

    QString selectedPageId() const;

    // The breadcrumb for the selected page: the space's name, then every
    // ancestor's title, then the page's own.
    QStringList trailFor(const QString& pageId) const;

signals:
    void pageSelected(const QString& pageId);

    // Something was published, so the page on the right is now stale.
    void published();

private:
    void updateButtons();

    // The title to show: the draft's when there is an unpublished change,
    // and the published one otherwise — so the tree reads like what the
    // author is working on.
    QString titleFor(const DocumentPage& page) const;

    DocumentPageRepository& m_pages;
    DocumentPageMoves&      m_moves;
    DocumentPageRemovals&   m_removals;
    DocumentRevisionStore&  m_revisions;
    DocumentPublisher&      m_publisher;

    // What each button asks the user before it writes (§5, §6.3).
    std::unique_ptr<DocumentTreeActions> m_actions;

    QTreeWidget* m_tree            = nullptr;
    QPushButton* m_newButton       = nullptr;
    QPushButton* m_upButton        = nullptr;
    QPushButton* m_downButton      = nullptr;
    QPushButton* m_moveToButton    = nullptr;
    QPushButton* m_deleteButton    = nullptr;
    QPushButton* m_withdrawButton  = nullptr;
    QPushButton* m_publishButton   = nullptr;

    DocumentSpace             m_space;
    bool                      m_editable = false;
    QList<DocumentPage>       m_loaded;
    QHash<QString, QString>   m_titles; // page id → the title shown
};
