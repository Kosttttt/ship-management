#pragma once

#include <QDateTime>
#include <QString>

// One page of a space, as the tree and the editor see it
// (document-management-spec.md §4.2).
//
// It holds **the draft, and only the draft**. The published title and content
// are those of the page's latest revision and are deliberately not copied
// here: two copies of the same text are two things to keep in step, and the
// one that is wrong is always the one being read.
struct DocumentPage {
    QString id;
    QString spaceId;

    // Empty means top of the tree.
    QString parentId;

    int position = 0;

    // Empty when there is no unpublished change. A page with no revision at
    // all is a draft page, which is how every page starts.
    QString draftTitle;
    QString draftContent;

    // A published page in a Manual space marked for removal in the manual's
    // next revision (§6.3). Still visible, struck through, until then.
    bool withdrawPending = false;

    bool hasDraft() const { return !draftTitle.isEmpty() || !draftContent.isEmpty(); }
};

// One published revision of one page. Immutable once written: this is the
// text that was in force and the title it had at the time.
struct DocumentPageRevision {
    QString id;
    QString pageId;

    // The page's own count, which is what a Collection page shows.
    int number = 0;

    // The manual revision this belongs to, in a Manual space; empty in a
    // Collection space. This is what lets a page show the *manual's* number.
    QString spaceRevisionId;

    QString title;
    QString content;
    QString note;

    // When it was published. Stored UTC, shown local (CLAUDE.md §6.2).
    QDateTime publishedAtUtc;
};

// One published revision of a whole manual. Immutable.
struct DocumentSpaceRevision {
    QString   id;
    QString   spaceId;
    int       number = 0;
    QString   note;
    QDateTime publishedAtUtc;
};

// What the page view needs to show one page: the page, whichever revision is
// current, and the manual revision number to display when there is one.
//
// Assembled where the rows are read rather than by the screen, because which
// revision is "current" is a rule (the highest live number) and not a
// formatting choice (CLAUDE.md §4 rule 3).
struct DocumentPageState {
    DocumentPage page;

    bool                 hasPublished = false;
    DocumentPageRevision published;

    // The manual's number for `published`, when the page is in a Manual
    // space. 0 in a Collection space, where the page's own number is shown.
    int manualNumber = 0;

    // The number and date the revision line shows: the manual's in a Manual
    // space, the page's own in a Collection space.
    int displayNumber() const { return manualNumber > 0 ? manualNumber : published.number; }
};
