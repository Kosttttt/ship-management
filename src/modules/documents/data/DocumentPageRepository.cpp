#include "modules/documents/data/DocumentPageRepository.h"

#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QHash>
#include <QUuid>

namespace {

// Appends one page's live children, in position order, then recurses — so the
// list comes back with every parent before its own children, which is the
// order a tree can be filled in without looking anything up twice.
void appendBranch(const QHash<QString, QList<DocumentPage>>& byParent, const QString& parentId,
                  QList<DocumentPage>* out)
{
    for (const DocumentPage& page : byParent.value(parentId)) {
        out->append(page);
        appendBranch(byParent, page.id, out);
    }
}

} // namespace

DocumentPageRepository::DocumentPageRepository(QSqlDatabase&            database,
                                               DocumentSpaceRepository& spaces)
    : m_database(database)
    , m_spaces(spaces)
{
}

QString DocumentPageRepository::columns()
{
    // Positional, and read back positionally in fromRow(). Anything new goes
    // on the END of this list, never into the middle.
    return QStringLiteral("id, space_id, parent_id, position, draft_title, draft_content,"
                          " withdraw_pending");
}

DocumentPage DocumentPageRepository::fromRow(const QSqlQuery& query)
{
    DocumentPage page;
    page.id      = query.value(0).toString();
    page.spaceId = query.value(1).toString();
    // NULL reads back as an empty QString, which is what "top of the tree"
    // means here.
    page.parentId        = query.value(2).toString();
    page.position        = query.value(3).toInt();
    page.draftTitle      = query.value(4).toString();
    page.draftContent    = query.value(5).toString();
    page.withdrawPending = query.value(6).toInt() != 0;
    return page;
}

bool DocumentPageRepository::listTree(const QString& spaceId, QList<DocumentPage>* pages)
{
    m_errorString.clear();
    pages->clear();

    // Through the space repository, so a space this installation cannot see
    // has no pages here either — by id or any other route.
    std::optional<DocumentSpace> space;
    if (!m_spaces.findById(spaceId, &space)) {
        m_errorString = m_spaces.errorString();
        return false;
    }
    if (!space.has_value()) {
        return true; // not visible: no pages, and not an error
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document_page"
                                 " WHERE space_id = ? AND is_deleted = 0"
                                 " ORDER BY position, draft_title COLLATE NOCASE")
                      .arg(columns()));
    query.addBindValue(spaceId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the pages:\n%1").arg(query.lastError().text());
        return false;
    }

    QHash<QString, QList<DocumentPage>> byParent;
    while (query.next()) {
        const DocumentPage page = fromRow(query);
        byParent[page.parentId].append(page);
    }

    // Top-level pages carry an empty parent id.
    appendBranch(byParent, QString(), pages);
    return true;
}

bool DocumentPageRepository::findById(const QString& id, std::optional<DocumentPage>* page)
{
    m_errorString.clear();
    page->reset();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document_page"
                                 " WHERE id = ? AND is_deleted = 0")
                      .arg(columns()));
    query.addBindValue(id);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the page:\n%1").arg(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        return true; // "not found" is a successful answer
    }

    const DocumentPage found = fromRow(query);

    // The space decides whether this page is visible at all.
    std::optional<DocumentSpace> space;
    if (!m_spaces.findById(found.spaceId, &space)) {
        m_errorString = m_spaces.errorString();
        return false;
    }
    if (!space.has_value()) {
        return true; // another vessel's page: not visible, not an error
    }

    *page = found;
    return true;
}

bool DocumentPageRepository::pageForWriting(const QString& pageId, DocumentPage* page)
{
    std::optional<DocumentPage> found;
    if (!findById(pageId, &found)) {
        return false;
    }
    if (!found.has_value()) {
        m_errorString = QStringLiteral("This page is no longer there.");
        return false;
    }

    std::optional<DocumentSpace> space;
    if (!m_spaces.findById(found->spaceId, &space) || !space.has_value()) {
        m_errorString = QStringLiteral("This page's space is no longer there.");
        return false;
    }
    if (!m_spaces.mayEdit(*space)) {
        m_errorString = QStringLiteral(
            "This space is managed at the office and cannot be changed here.");
        return false;
    }

    *page = *found;
    return true;
}

bool DocumentPageRepository::mayWrite(const QString& pageId, QString* why)
{
    m_errorString.clear();

    DocumentPage page;
    if (!pageForWriting(pageId, &page)) {
        *why = m_errorString;
        return false;
    }
    why->clear();
    return true;
}

bool DocumentPageRepository::create(const QString& spaceId, const QString& parentId,
                                    const QString& title, QString* newId)
{
    m_errorString.clear();

    std::optional<DocumentSpace> space;
    if (!m_spaces.findById(spaceId, &space)) {
        m_errorString = m_spaces.errorString();
        return false;
    }
    if (!space.has_value()) {
        m_errorString = QStringLiteral("This space is no longer there.");
        return false;
    }
    if (!m_spaces.mayEdit(*space)) {
        m_errorString = QStringLiteral(
            "This space is managed at the office and cannot be changed here.");
        return false;
    }
    if (title.trimmed().isEmpty()) {
        m_errorString = QStringLiteral("A page needs a title.");
        return false;
    }

    // Last among its siblings, so a new page appears where it was added
    // rather than somewhere in the middle.
    int       position = 0;
    QSqlQuery next(m_database);
    next.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM document_page"
                                " WHERE space_id = ? AND is_deleted = 0"
                                "   AND %1")
                     .arg(DocumentSql::siblingCondition()));
    next.addBindValue(spaceId);
    DocumentSql::bindSiblingCondition(next, parentId);
    if (next.exec() && next.next()) {
        position = next.value(0).toInt();
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = DocumentSql::nowUtc();

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO document_page"
        " (id, space_id, parent_id, position, draft_title, draft_content, withdraw_pending,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, '', 0, ?, ?, ?, ?, 0, ?, 1)"));
    insert.addBindValue(id);
    insert.addBindValue(spaceId);
    insert.addBindValue(DocumentSql::nullableText(parentId));
    insert.addBindValue(position);
    // The title goes in as the draft: a new page is unpublished until
    // somebody publishes it, title included.
    insert.addBindValue(title.trimmed());
    insert.addBindValue(now);
    insert.addBindValue(DocumentSql::systemUser());
    insert.addBindValue(now);
    insert.addBindValue(DocumentSql::systemUser());
    insert.addBindValue(QStringLiteral("LOCAL"));

    if (!insert.exec()) {
        m_errorString =
            QStringLiteral("Could not create the page:\n%1").arg(insert.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool DocumentPageRepository::saveDraft(const QString& pageId, const QString& title,
                                       const QString& content)
{
    m_errorString.clear();

    DocumentPage page;
    if (!pageForWriting(pageId, &page)) {
        return false;
    }
    if (title.trimmed().isEmpty()) {
        m_errorString = QStringLiteral("A page needs a title.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE document_page SET"
                                 "  draft_title = ?, draft_content = ?,"
                                 "  updated_at = ?, updated_by = ?, revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(title.trimmed());
    query.addBindValue(content);
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(pageId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not save the draft:\n%1").arg(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DocumentPageRepository::clearDraft(const QString& pageId)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    // No permission check: publishing calls this immediately after writing a
    // revision, inside the same transaction, having already been allowed —
    // and a second check there could only fail where the first succeeded.
    query.prepare(QStringLiteral("UPDATE document_page SET"
                                 "  draft_title = '', draft_content = '',"
                                 "  updated_at = ?, updated_by = ?, revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(pageId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not clear the draft:\n%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

QString DocumentPageRepository::errorString() const
{
    return m_errorString;
}
