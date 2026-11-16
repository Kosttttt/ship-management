#pragma once

#include <QString>

class DocumentHistoryReader;
class DocumentPageRepository;

// Putting an old revision of a page back (document-history-spec.md §5).
//
// **It restores into the draft and never publishes.** That is the whole rule,
// and the reason this is a service and not a button: restoring is an edit
// like any other, so it goes out the normal way — on its own in a Collection
// space, or as part of the manual's next revision — and ISM's rule that only
// a published revision counts stays true. A Restore that wrote a new
// published revision by itself would put a manual at Rev 9 with no statement
// of what changed, and a page back in force that nobody had agreed to.
//
// Refusing it on a read-only space happens **here**, not only in the greying
// of a button: a vessel cannot restore into a company manual (§4.3), and the
// screen is not where that is decided (CLAUDE.md §4 rule 3).
class DocumentRestorer
{
public:
    DocumentRestorer(DocumentPageRepository& pages, DocumentHistoryReader& history);

    // Copies that revision's title and content into its page's draft,
    // replacing whatever draft was there. Whether to ask first is the
    // screen's business, which is why this takes no "confirmed" flag — a
    // service that trusted one would be a service that could be told not to
    // ask.
    //
    // Pictures need nothing copied: the revision refers to images that are
    // still there, because `document_image` is never changed (§5).
    bool restoreIntoDraft(const QString& pageRevisionId);

    QString errorString() const;

private:
    DocumentPageRepository& m_pages;
    DocumentHistoryReader&  m_history;
    QString                 m_errorString;
};
