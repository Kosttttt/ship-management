#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class DocumentPageRemovals;
class DocumentPageRepository;
class DocumentRevisionStore;
class DocumentSpaceRepository;
class QSqlDatabase;

// Turning drafts into published revisions (document-management-spec.md §6.2).
//
// In the service layer because publishing is a *sequence whose order is the
// rule*: work out the number, write the manual's revision, write a revision
// for each changed page, withdraw what was marked, clear the drafts — and
// either all of it or none. Four screens could not be trusted to do that in
// the same order, and one of them doing it differently is a half-published
// manual.
class DocumentPublisher
{
public:
    DocumentPublisher(QSqlDatabase& database, DocumentSpaceRepository& spaces,
                      DocumentPageRepository& pages, DocumentPageRemovals& removals,
                      DocumentRevisionStore& revisions);

    // One line of the Publish revision… dialog (§6.2).
    struct PendingChange {
        enum class Kind {
            New,      // never published
            Changed,  // published, and edited since
            Withdrawn // published, and marked to be removed (§6.3)
        };

        QString pageId;
        QString title;
        Kind    kind = Kind::Changed;
    };

    // **Collection space, one page.** The page gets its next number; the note
    // is optional, because a circular replaced by a newer one needs no
    // explanation beyond being newer.
    //
    // Refused for a page in a Manual space: there, a page is published as
    // part of its manual's revision and never on its own (§6.2).
    bool publishPage(const QString& pageId, const QString& note);

    // Everything a manual's next revision would cover, in tree order so the
    // dialog reads like the manual.
    bool pendingChanges(const QString& spaceId, QList<PendingChange>* changes);

    // **Manual space, the whole manual.** Creates the space revision and one
    // page revision for each pending change, withdraws what was marked, and
    // clears the drafts — all in **one transaction**, so a half-published
    // manual is impossible.
    //
    // The note is required here: a revision of a controlled manual with no
    // statement of what changed is not a record of anything.
    bool publishManual(const QString& spaceId, const QString& note, int* newNumber = nullptr);

    // The titles a manual revision withdrew.
    //
    // Worked out structurally rather than from a marker: a page revision
    // belonging to this manual revision whose page is no longer in the tree
    // is a page this revision removed. In a Manual space a published page is
    // only ever removed by being withdrawn (§6.3), so there is nothing else
    // it could be — and no extra column that a later step might have wanted
    // to mean something slightly different.
    bool listWithdrawnIn(const QString& spaceRevisionId, QStringList* titles);

    QString errorString() const;

private:
    QSqlDatabase&            m_database;
    DocumentSpaceRepository& m_spaces;
    DocumentPageRepository&  m_pages;
    // Withdrawal, which is a removal and lives with the other removals.
    DocumentPageRemovals&   m_removals;
    DocumentRevisionStore&   m_revisions;
    QString                  m_errorString;
};
