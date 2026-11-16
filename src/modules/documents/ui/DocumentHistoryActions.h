#pragma once

#include "modules/documents/domain/DocumentPage.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QString>

class DocumentHistoryReader;
class DocumentImageStore;
class DocumentPageRepository;
class DocumentRestorer;
class DocumentRevisionStore;
class QWidget;

// The screen's history, comparison and restore actions: what History…,
// Revisions… and Compare with published open, and what they do with the
// answer (document-history-spec.md §2, §3, §4.1, §5).
//
// Split out of DocumentsWidget the same way DocumentTreeActions came out of
// DocumentTreePanel: the screen knows what is selected, and this knows which
// dialog that opens and what to re-read afterwards. Neither decides a rule —
// which revisions exist is the reader's answer and whether a restore is
// allowed is the service's (CLAUDE.md §4 rule 3).
class DocumentHistoryActions
{
public:
    DocumentHistoryActions(DocumentPageRepository& pages, DocumentRevisionStore& revisions,
                           DocumentHistoryReader& history, DocumentRestorer& restorer,
                           DocumentImageStore& images, QWidget* dialogParent);

    // §2. True when a revision was restored into the draft, so the caller
    // re-reads the page and the tree.
    bool showHistory(const DocumentPageState& state, const DocumentSpace& space,
                     const QString& pageTitle, bool editable);

    // §3, for a Manual space.
    void showRevisions(const DocumentSpace& space);

    // §4.1. Takes a page id rather than a state, because the Publish
    // revision… dialog offers this against each page it lists and has only
    // the id — and because the draft and the published revision are read
    // here either way.
    void compareDraftWithPublished(const QString& pageId);

private:
    DocumentPageRepository& m_pages;
    DocumentRevisionStore&  m_revisions;
    DocumentHistoryReader&  m_history;
    DocumentRestorer&       m_restorer;
    DocumentImageStore&     m_images;

    // Where the dialogs appear: the screen, so they are centred on the
    // window the user is looking at.
    QWidget* m_parent = nullptr;
};
