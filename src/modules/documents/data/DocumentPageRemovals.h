#pragma once

#include <QString>
#include <QStringList>

class DocumentPageRepository;
class QSqlDatabase;

// Taking a page out of a space: marking it for withdrawal, and soft-deleting
// it (document-management-spec.md §6.3).
//
// Split out of DocumentPageRepository, which was 447 lines, by the rule in
// CLAUDE.md §9 — and along a real seam rather than an arbitrary one. §6.3 is
// a set of rules of its own: a draft page is deleted outright, a published
// page in a manual is withdrawn with the manual's next revision instead, and
// deleting a page takes everything beneath it. None of that is needed by
// anything that only reads or edits a page.
//
// SQL, so it stays in the repository layer (CLAUDE.md §4 rule 2), and it is
// used only by `document_page`'s own repository and its callers — the same
// shape as DocumentPageMoves, which owns tree order.
//
// **Permission is asked, not re-decided.** Every function here goes through
// DocumentPageRepository::mayWrite() first, so §4.3 has one implementation
// and not three.
class DocumentPageRemovals
{
public:
    DocumentPageRemovals(QSqlDatabase& database, DocumentPageRepository& pages);

    // §6.3: marked for withdrawal in a Manual space's next revision, and
    // "Undo withdraw" until then.
    bool setWithdrawPending(const QString& pageId, bool pending);

    // Soft delete (CLAUDE.md §6.4) of one page. The caller decides whether
    // that is the right thing to do — §6.3 says a published page in a Manual
    // space is withdrawn instead, which is the screen's rule to apply because
    // it is the one that knows what the user was asked.
    bool softDelete(const QString& pageId);

    // The page and everything beneath it, in one go, for the confirmation
    // that names how many pages go with it (§6.3).
    bool softDeleteWithDescendants(const QString& pageId, int* deletedCount = nullptr);

    // Every page of a space, for when the space itself is deleted (§5).
    bool softDeleteSpacePages(const QString& spaceId);

    // How many live pages a space has — what the delete-space confirmation
    // says goes with it (§5). Here rather than on the repository because it
    // exists for the sake of the deletion above.
    bool countSpacePages(const QString& spaceId, int* count);

    // How many live pages sit beneath this one, at any depth. What the
    // confirmation counts.
    bool countDescendants(const QString& pageId, int* count);

    QString errorString() const;

private:
    // The ids of this page and all its descendants, deepest last.
    bool descendantIds(const QString& pageId, QStringList* ids);

    bool markDeleted(const QStringList& pageIds);

    QSqlDatabase&           m_database;
    DocumentPageRepository& m_pages;
    QString                 m_errorString;
};
