#pragma once

#include "modules/documents/domain/DocumentPage.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QHash>
#include <QList>
#include <QString>

class DocumentPageMoves;
class DocumentPageRemovals;
class DocumentPageRepository;
class DocumentPublisher;
class DocumentRevisionStore;
class QWidget;

// The page tree's buttons, each of which asks the user something before it
// writes: New Page, Move to…, Delete…, Undo withdraw and Publish revision…
// (document-management-spec.md §5, §6.2, §6.3).
//
// Split out of DocumentTreePanel, which was 430 lines, by the rule in
// CLAUDE.md §9 — the same shape as VesselArchiveActions out of
// VesselListWidget. The seam: the panel draws the tree and knows what is
// selected; this knows what each button asks and what it then calls. Neither
// decides any rule, which is the repositories' and the publisher's job
// (CLAUDE.md §4 rule 3).
//
// Every function **returns true when something changed**, so the panel
// reloads in one place rather than each action remembering to.
class DocumentTreeActions
{
public:
    DocumentTreeActions(DocumentPageRepository& pages, DocumentPageMoves& moves,
                        DocumentPageRemovals& removals, DocumentRevisionStore& revisions,
                        DocumentPublisher& publisher, QWidget* dialogParent);

    // A child of `parentId`, or a top-level page when it is empty — which is
    // how a space's first page gets made (§5).
    bool createPage(const DocumentSpace& space, const QString& parentId);

    // Offers the other pages of the space as the new parent. `pages` and
    // `titles` are the tree as the panel has it, because the panel is what
    // knows how a page is titled.
    bool moveTo(const QString& pageId, const QList<DocumentPage>& pages,
                const QHash<QString, QString>& titles);

    // §6.3, until the revision is published.
    bool undoWithdraw(const QString& pageId);

    // §6.3's three cases: a published page of a manual is marked for
    // withdrawal, a published page or one with pages beneath it is confirmed
    // first, and a never-published page with nothing beneath it goes
    // straight away.
    bool deleteOrWithdraw(const DocumentSpace& space, const QString& pageId,
                          const QString& title);

    // §6.2: the whole manual, with the changed and withdrawn pages listed and
    // a note required.
    bool publishManual(const DocumentSpace& space);

private:
    DocumentPageRepository& m_pages;
    DocumentPageMoves&      m_moves;
    DocumentPageRemovals&   m_removals;
    DocumentRevisionStore&  m_revisions;
    DocumentPublisher&      m_publisher;

    // Where the dialogs appear. The panel, so they are centred on the screen
    // the user is looking at.
    QWidget* m_parent = nullptr;
};
