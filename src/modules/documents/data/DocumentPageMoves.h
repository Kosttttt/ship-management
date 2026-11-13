#pragma once

#include <QString>

class DocumentPageRepository;
class QSqlDatabase;

// Where a page sits in the tree: Move up, Move down, Move to… (§5).
//
// Split out of DocumentPageRepository, which was over the ~300-line guideline
// with both in it (CLAUDE.md §9). The seam is a real one: everything here is
// about order and parentage, and nothing here touches a page's title, its
// draft or its revisions.
//
// It holds SQL for `document_page` and is used only by its own repository,
// which is what §9 allows for a helper in the repository layer.
class DocumentPageMoves
{
public:
    DocumentPageMoves(QSqlDatabase& database, DocumentPageRepository& pages);

    // Swaps the page with the sibling above or below it. Doing nothing at
    // either end is success: the user asked for it to be as high as it can
    // go, and it is.
    bool moveUp(const QString& pageId);
    bool moveDown(const QString& pageId);

    // Re-parents the page. An empty newParentId moves it to the top of the
    // tree. It lands last among its new siblings.
    //
    // **Refused when the new parent is the page itself or one of its own
    // descendants.** That would cut the branch off the tree entirely: the
    // page would be its own ancestor, invisible from the top and impossible
    // to reach again without editing the database.
    bool moveTo(const QString& pageId, const QString& newParentId);

    QString errorString() const;

private:
    // The sibling immediately above or below, or empty when there is none.
    bool neighbour(const QString& pageId, bool above, QString* neighbourId);

    bool swapPositions(const QString& firstId, const QString& secondId);

    // Whether `candidateId` is `pageId` itself or sits beneath it.
    bool isSelfOrDescendant(const QString& pageId, const QString& candidateId, bool* result);

    QSqlDatabase&           m_database;
    DocumentPageRepository& m_pages;
    QString                 m_errorString;
};
