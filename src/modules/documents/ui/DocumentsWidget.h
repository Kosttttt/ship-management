#pragma once

#include "modules/documents/domain/DocumentSpace.h"

#include <QString>
#include <QWidget>

class DocumentImageStore;
class DocumentPageMoves;
class DocumentPageRemovals;
class DocumentPageRepository;
class DocumentPageView;
class DocumentPublisher;
class DocumentRevisionStore;
class DocumentSpacePanel;
class DocumentSpaceRepository;
class DocumentTreePanel;

// The Documents screen (document-management-spec.md §5): spaces and the page
// tree on the left, the page on the right.
//
// It owns nothing but the three panels and the wiring between them — which
// space is chosen, which page, and what Save means. Every rule it needs is
// asked of the repositories rather than kept here (CLAUDE.md §4 rule 3).
class DocumentsWidget : public QWidget
{
    Q_OBJECT

public:
    DocumentsWidget(DocumentSpaceRepository& spaces, DocumentPageRepository& pages,
                    DocumentPageMoves& moves, DocumentPageRemovals& removals,
                    DocumentRevisionStore& revisions, DocumentPublisher& publisher,
                    DocumentImageStore& images, QWidget* parent = nullptr);

private:
    void spaceChanged(const QString& spaceId);
    void pageChanged(const QString& pageId);

    // Re-reads the selected page and redraws the right-hand side. Called
    // after anything that could have changed it, rather than each caller
    // working out what to refresh.
    void refreshPage();

    void saveDraft();
    void discardDraft();
    void publishPage();

    DocumentSpaceRepository& m_spaces;
    DocumentPageRepository&  m_pages;
    DocumentRevisionStore&   m_revisions;
    DocumentPublisher&       m_publisher;

    DocumentSpacePanel* m_spacePanel = nullptr;
    DocumentTreePanel*  m_treePanel  = nullptr;
    DocumentPageView*   m_pageView   = nullptr;

    DocumentSpace m_space;
    QString       m_pageId;
};
