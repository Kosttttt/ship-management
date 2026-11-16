#pragma once

#include "modules/documents/domain/DocumentPage.h"

#include <QList>
#include <QString>

#include <optional>

class DocumentPageRepository;
class DocumentSpaceRepository;
class QSqlDatabase;

// Reading what has been published: a page's history, and a manual's Record of
// Revisions (document-history-spec.md §2, §3).
//
// Read-only, and its own class rather than more methods on
// DocumentRevisionStore — which is already at the size guideline (CLAUDE.md
// §9) and has a different job: that one is what publishing writes through and
// what the page view asks "which revision is in force", while this answers
// the questions the two history screens ask. Both hold SQL, so both stay in
// the repository layer (CLAUDE.md §4 rule 2).
//
// **Visibility is enforced here as everywhere else** (§4.3). History, View
// and Compare are allowed on a read-only space — a vessel may read a company
// manual's history — but another vessel's space is not readable at all, by id
// or any other route, so every question here goes through the repository that
// already owns that rule rather than keeping a second copy of it.
class DocumentHistoryReader
{
public:
    // One line of a page's history (§2).
    struct PageHistoryEntry {
        DocumentPageRevision revision;

        // The manual's number for this revision, in a Manual space; 0 in a
        // Collection space, where a page's own count is all there is.
        int manualNumber = 0;

        // The manual revision's note, in a Manual space.
        //
        // Carried separately because of where a note is actually written: a
        // manual is published as a whole with **one** statement of what
        // changed (§6.2 of the module spec), so the note belongs to the
        // manual's revision and a page revision of a manual has none of its
        // own. The history list needs the note the reader means, and the
        // immutable page revision is not rewritten to pretend it has one.
        QString manualNote;

        // What the history list shows in its Note column: the page's own
        // note, or the manual revision's when the page has none.
        QString shownNote() const
        {
            return revision.note.isEmpty() ? manualNote : revision.note;
        }
    };

    // One page listed under a manual revision (§3).
    struct RevisionPage {
        enum class Kind {
            New,       // this revision is the page's first
            Changed,   // it had a revision before this one
            Withdrawn, // this revision took it out of the manual (§6.3)
        };

        QString pageId;
        QString pageRevisionId;
        QString title;
        Kind    kind = Kind::Changed;
    };

    DocumentHistoryReader(QSqlDatabase& database, DocumentSpaceRepository& spaces,
                          DocumentPageRepository& pages);

    // Newest first, which is the order the history list shows (§2).
    bool listPageHistory(const QString& pageId, QList<PageHistoryEntry>* entries);

    // One revision, to open read-only or to compare. Reachable for a page
    // that has since been withdrawn, because the Record of Revisions still
    // lists it (§6) — so this is scoped by the space, not by whether the page
    // is still in the tree.
    bool findPageRevision(const QString&                       revisionId,
                          std::optional<DocumentPageRevision>* revision);

    // The revision published immediately before this one, for "Compare with
    // previous". Empty when it is the page's first.
    bool previousPageRevision(const DocumentPageRevision&          revision,
                              std::optional<DocumentPageRevision>* previous);

    // A manual's revisions, newest first (§3).
    bool listSpaceRevisions(const QString& spaceId, QList<DocumentSpaceRevision>* revisions);

    // What one manual revision covered, marked new, changed or withdrawn.
    //
    // Worked out from the revisions themselves rather than from a column
    // saying so: a page's first revision is new, a page that has left the
    // tree and whose last revision this is was withdrawn by it, and anything
    // else changed. That is the same structural reading
    // DocumentPublisher::listWithdrawnIn() uses, and for the same reason — a
    // stored marker is a second answer that can disagree with the first.
    bool listRevisionPages(const QString& spaceRevisionId, QList<RevisionPage>* pages);

    QString errorString() const;

private:
    // Whether this installation may read the space a revision belongs to.
    bool spaceIsVisible(const QString& spaceId, bool* visible);

    QSqlDatabase&            m_database;
    DocumentSpaceRepository& m_spaces;
    DocumentPageRepository&  m_pages;
    QString                  m_errorString;
};
