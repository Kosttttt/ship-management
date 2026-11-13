#pragma once

#include "modules/documents/domain/DocumentPage.h"

#include <QList>
#include <QString>

#include <optional>

class QSqlDatabase;

// `document_page_revision` and `document_space_revision` — what has been
// published (document-management-spec.md §4.2).
//
// **Both tables are immutable**, so there is no update here and never will
// be: a published revision of a controlled manual that could be edited
// afterwards is not a record of anything. Writing is append-only and reading
// is the whole of the rest.
//
// No permission checks: every caller is a repository or the publisher, which
// has already asked whether this write is allowed. A second copy of that
// question is how two answers start to disagree.
class DocumentRevisionStore
{
public:
    DocumentRevisionStore(QSqlDatabase& database, const QString& nodeId);

    // The revision in force for a page, if it has ever been published.
    bool latestPageRevision(const QString& pageId, std::optional<DocumentPageRevision>* revision);

    // Newest first, which is what a history list wants (step 2 of this
    // module). Used here to find the current one and to count.
    bool listPageRevisions(const QString& pageId, QList<DocumentPageRevision>* revisions);

    bool latestSpaceRevision(const QString&                          spaceId,
                             std::optional<DocumentSpaceRevision>*   revision);

    // The page plus whichever revision is current plus, in a Manual space,
    // the manual's number for it — which is what the revision line shows.
    //
    // Assembled here rather than by the screen because "current" is a rule
    // (the highest live number) and not a formatting choice
    // (CLAUDE.md §4 rule 3).
    bool stateFor(const DocumentPage& page, bool spaceIsManual, DocumentPageState* state);

    // One past the highest number this page has, deleted revisions included,
    // so a number is never reused.
    bool nextPageNumber(const QString& pageId, int* number);
    bool nextSpaceNumber(const QString& spaceId, int* number);

    bool insertSpaceRevision(const QString& spaceId, int number, const QString& note,
                             QString* newId);

    // `spaceRevisionId` is empty in a Collection space, where a page's own
    // number is all there is.
    bool insertPageRevision(const QString& pageId, int number, const QString& spaceRevisionId,
                            const QString& title, const QString& content, const QString& note,
                            QString* newId = nullptr);

    QString errorString() const;

private:
    QSqlDatabase& m_database;
    QString       m_nodeId;
    QString       m_errorString;
};
