#include "modules/documents/service/DocumentRestorer.h"

#include "modules/documents/data/DocumentHistoryReader.h"
#include "modules/documents/data/DocumentPageRepository.h"

#include <optional>

DocumentRestorer::DocumentRestorer(DocumentPageRepository& pages, DocumentHistoryReader& history)
    : m_pages(pages)
    , m_history(history)
{
}

bool DocumentRestorer::restoreIntoDraft(const QString& pageRevisionId)
{
    m_errorString.clear();

    std::optional<DocumentPageRevision> revision;
    if (!m_history.findPageRevision(pageRevisionId, &revision)) {
        m_errorString = m_history.errorString();
        return false;
    }
    if (!revision.has_value()) {
        m_errorString = QStringLiteral("That revision is no longer there.");
        return false;
    }

    // The page itself must still be in the tree, and this is asked first
    // because it is the case with something to explain: a page withdrawn
    // from a manual is deliberately not restorable in this step (§6). Its
    // history stays readable through the Record of Revisions, and bringing a
    // withdrawn section back is a decision with its own consequences for the
    // manual's next revision, so it waits for a step that can state them.
    //
    // Asking before the write rule leaks nothing: findById() already applies
    // §4.3's visibility, so a page in a space this installation cannot see
    // gives the same answer as a page that is not there — which is the only
    // thing anybody here is entitled to know about it.
    std::optional<DocumentPage> page;
    if (!m_pages.findById(revision->pageId, &page)) {
        m_errorString = m_pages.errorString();
        return false;
    }
    if (!page.has_value()) {
        m_errorString = QStringLiteral(
            "That page is no longer in the tree, so there is nothing to restore it into.");
        return false;
    }

    // §4.3's write rule, asked of the repository that owns it. A vessel may
    // read a company manual's history and compare its revisions; it may not
    // restore one.
    if (!m_pages.mayWrite(page->id, &m_errorString)) {
        return false;
    }

    // The draft, and only the draft. Nothing here writes a revision.
    if (!m_pages.saveDraft(page->id, revision->title, revision->content)) {
        m_errorString = m_pages.errorString();
        return false;
    }
    return true;
}

QString DocumentRestorer::errorString() const
{
    return m_errorString;
}
