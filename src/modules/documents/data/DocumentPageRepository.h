#pragma once

#include "modules/documents/domain/DocumentPage.h"

#include <QList>
#include <QString>

#include <optional>

class DocumentSpaceRepository;
class QSqlDatabase;
class QSqlQuery;

// Reads and writes `document_page`: the tree, one page, and its draft. The
// only file holding SQL for that, apart from its own three helpers —
// DocumentPageMoves for tree order, DocumentPageRemovals for withdrawal and
// deletion, and DocumentRevisionStore for what has been published
// (CLAUDE.md §4 rule 2, §9).
//
// **Every write asks the space first.** A page is only as editable as the
// space it is in, and that rule already has one implementation in
// DocumentSpaceRepository — so this holds a reference to it and asks, rather
// than keeping a second copy of §4.3 that could disagree. A page in a space
// this installation cannot see does not exist as far as this class is
// concerned, by id or any other route.
class DocumentPageRepository
{
public:
    DocumentPageRepository(QSqlDatabase& database, DocumentSpaceRepository& spaces);

    // The columns and how to read them back. Public and static because
    // DocumentPageMoves reads the same rows, and the positional list must
    // live in one place.
    static QString      columns();
    static DocumentPage fromRow(const QSqlQuery& query);

    // Every live page of one space, parents before children and siblings in
    // position order — which is the order a tree widget wants to be filled
    // in. Empty when the space is not visible here.
    bool listTree(const QString& spaceId, QList<DocumentPage>* pages);

    bool findById(const QString& id, std::optional<DocumentPage>* page);

    // Whether this page's space is this installation's to change (§4.3).
    // Public because DocumentPageMoves asks it before every move, and the
    // screen asks it to grey its buttons — one implementation of the rule,
    // consulted rather than copied.
    bool mayWrite(const QString& pageId, QString* why);

    // A new page, last among its siblings. An empty parentId puts it at the
    // top of the tree. It starts as a draft with no revision, which is what
    // makes it a draft page (§4.2).
    bool create(const QString& spaceId, const QString& parentId, const QString& title,
                QString* newId = nullptr);

    // Keeps the draft. Both fields together, because a title and its body are
    // one edit.
    bool saveDraft(const QString& pageId, const QString& title, const QString& content);

    // Throws the unpublished change away, and is also what publishing calls
    // once the revision is written — the published content is then what Edit
    // starts from (§6.2).
    bool clearDraft(const QString& pageId);

    // Marking a page for withdrawal and deleting it live in
    // DocumentPageRemovals, beside this class (§6.3).

    QString errorString() const;

private:
    // The page, and the space it is in, with the space's own visibility and
    // §4.3 write rule already applied. False with a message set when the page
    // is not there or not this installation's to change.
    bool pageForWriting(const QString& pageId, DocumentPage* page);

    QSqlDatabase&            m_database;
    DocumentSpaceRepository& m_spaces;
    QString                  m_errorString;
};
