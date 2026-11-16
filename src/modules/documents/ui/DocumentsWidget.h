#pragma once

#include "modules/documents/domain/DocumentPage.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QString>
#include <QWidget>

#include <memory>

class DocumentHistoryActions;
class DocumentHistoryReader;
class DocumentImageStore;
class DocumentPageMoves;
class DocumentPageRemovals;
class DocumentPageRepository;
class DocumentPageView;
class DocumentPublisher;
class DocumentRestorer;
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
                    DocumentHistoryReader& history, DocumentRestorer& restorer,
                    DocumentImageStore& images, QWidget* parent = nullptr);

    // Out of line because m_historyActions holds a type this header only
    // names.
    ~DocumentsWidget() override;

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

    // §2: History… on the selected page. A restore from it changes the
    // draft, so the tree and the page are re-read.
    void showHistory();

    // The page's state as the panels have it, so History and Compare ask the
    // same question of the same page the screen is showing.
    bool currentState(DocumentPageState* state);

    DocumentSpaceRepository& m_spaces;
    DocumentPageRepository&  m_pages;
    DocumentRevisionStore&   m_revisions;
    DocumentPublisher&       m_publisher;

    // What History…, Revisions… and Compare with published open
    // (document-history-spec.md §2, §3, §4.1).
    std::unique_ptr<DocumentHistoryActions> m_historyActions;

    DocumentSpacePanel* m_spacePanel = nullptr;
    DocumentTreePanel*  m_treePanel  = nullptr;
    DocumentPageView*   m_pageView   = nullptr;

    DocumentSpace m_space;
    QString       m_pageId;
};
