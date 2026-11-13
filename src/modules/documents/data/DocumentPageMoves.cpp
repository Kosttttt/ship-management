#include "modules/documents/data/DocumentPageMoves.h"

#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <optional>

DocumentPageMoves::DocumentPageMoves(QSqlDatabase& database, DocumentPageRepository& pages)
    : m_database(database)
    , m_pages(pages)
{
}

bool DocumentPageMoves::neighbour(const QString& pageId, bool above, QString* neighbourId)
{
    neighbourId->clear();

    std::optional<DocumentPage> page;
    if (!m_pages.findById(pageId, &page)) {
        m_errorString = m_pages.errorString();
        return false;
    }
    if (!page.has_value()) {
        m_errorString = QStringLiteral("This page is no longer there.");
        return false;
    }

    // Among siblings only: moving up must not jump a page out of its branch.
    // The parent comparison has to cope with NULL for a top-level page, which
    // is what siblingCondition() is for.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM document_page"
                                 " WHERE space_id = ? AND is_deleted = 0"
                                 "   AND %1"
                                 "   AND position %2 ?"
                                 " ORDER BY position %3 LIMIT 1")
                      .arg(DocumentSql::siblingCondition(),
                           above ? QStringLiteral("<") : QStringLiteral(">"),
                           above ? QStringLiteral("DESC") : QStringLiteral("ASC")));
    query.addBindValue(page->spaceId);
    DocumentSql::bindSiblingCondition(query, page->parentId);
    query.addBindValue(page->position);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the page order:\n%1").arg(query.lastError().text());
        return false;
    }
    if (query.next()) {
        *neighbourId = query.value(0).toString();
    }
    return true;
}

bool DocumentPageMoves::swapPositions(const QString& firstId, const QString& secondId)
{
    std::optional<DocumentPage> first;
    std::optional<DocumentPage> second;
    if (!m_pages.findById(firstId, &first) || !m_pages.findById(secondId, &second)) {
        m_errorString = m_pages.errorString();
        return false;
    }
    if (!first.has_value() || !second.has_value()) {
        m_errorString = QStringLiteral("One of the pages is no longer there.");
        return false;
    }

    const QString now = DocumentSql::nowUtc();

    // Two pages can share a position for an instant between these two
    // statements. Harmless: the order is only read to draw the tree, and
    // nothing reads it in between.
    const QList<QPair<QString, int>> updates{{firstId, second->position},
                                             {secondId, first->position}};
    for (const QPair<QString, int>& update : updates) {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("UPDATE document_page SET"
                                     "  position = ?, updated_at = ?, updated_by = ?,"
                                     "  revision = revision + 1"
                                     " WHERE id = ? AND is_deleted = 0"));
        query.addBindValue(update.second);
        query.addBindValue(now);
        query.addBindValue(DocumentSql::systemUser());
        query.addBindValue(update.first);

        if (!query.exec()) {
            m_errorString = QStringLiteral("Could not reorder the pages:\n%1")
                                .arg(query.lastError().text());
            return false;
        }
    }
    return true;
}

bool DocumentPageMoves::moveUp(const QString& pageId)
{
    m_errorString.clear();

    if (!m_pages.mayWrite(pageId, &m_errorString)) {
        return false;
    }

    QString above;
    if (!neighbour(pageId, /*above=*/true, &above)) {
        return false;
    }
    if (above.isEmpty()) {
        return true; // already first: what was asked for is already true
    }
    return swapPositions(pageId, above);
}

bool DocumentPageMoves::moveDown(const QString& pageId)
{
    m_errorString.clear();

    if (!m_pages.mayWrite(pageId, &m_errorString)) {
        return false;
    }

    QString below;
    if (!neighbour(pageId, /*above=*/false, &below)) {
        return false;
    }
    if (below.isEmpty()) {
        return true; // already last
    }
    return swapPositions(pageId, below);
}

bool DocumentPageMoves::isSelfOrDescendant(const QString& pageId, const QString& candidateId,
                                           bool* result)
{
    *result = false;

    if (candidateId.isEmpty()) {
        return true; // the top of the tree is nobody's descendant
    }
    if (candidateId == pageId) {
        *result = true;
        return true;
    }

    // Walk up from the candidate: if the page is one of its ancestors, then
    // the candidate is beneath the page. Walking up is bounded by the depth
    // of the tree, while walking down is bounded by its size.
    QString current = candidateId;
    for (int guard = 0; guard < 1000 && !current.isEmpty(); ++guard) {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "SELECT parent_id FROM document_page WHERE id = ? AND is_deleted = 0"));
        query.addBindValue(current);

        if (!query.exec()) {
            m_errorString = QStringLiteral("Could not read the page tree:\n%1")
                                .arg(query.lastError().text());
            return false;
        }
        if (!query.next()) {
            return true; // reached a page that is gone: not beneath this one
        }

        current = query.value(0).toString();
        if (current == pageId) {
            *result = true;
            return true;
        }
    }
    return true;
}

bool DocumentPageMoves::moveTo(const QString& pageId, const QString& newParentId)
{
    m_errorString.clear();

    if (!m_pages.mayWrite(pageId, &m_errorString)) {
        return false;
    }

    std::optional<DocumentPage> page;
    if (!m_pages.findById(pageId, &page)) {
        m_errorString = m_pages.errorString();
        return false;
    }
    if (!page.has_value()) {
        m_errorString = QStringLiteral("This page is no longer there.");
        return false;
    }

    bool wouldLoop = false;
    if (!isSelfOrDescendant(pageId, newParentId, &wouldLoop)) {
        return false;
    }
    if (wouldLoop) {
        m_errorString = QStringLiteral(
            "A page cannot be moved into itself or into one of its own pages.");
        return false;
    }

    if (!newParentId.isEmpty()) {
        // The new parent has to be a page this installation can see, and in
        // the same space: a page cannot be moved into another manual.
        std::optional<DocumentPage> parent;
        if (!m_pages.findById(newParentId, &parent)) {
            m_errorString = m_pages.errorString();
            return false;
        }
        if (!parent.has_value()) {
            m_errorString = QStringLiteral("That page is no longer there.");
            return false;
        }
        if (parent->spaceId != page->spaceId) {
            m_errorString = QStringLiteral("A page can only be moved within its own space.");
            return false;
        }
    }

    // Last among its new siblings, so it appears where it was put.
    int       position = 0;
    QSqlQuery next(m_database);
    next.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM document_page"
                                " WHERE space_id = ? AND is_deleted = 0"
                                "   AND %1")
                     .arg(DocumentSql::siblingCondition()));
    next.addBindValue(page->spaceId);
    DocumentSql::bindSiblingCondition(next, newParentId);
    if (next.exec() && next.next()) {
        position = next.value(0).toInt();
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE document_page SET"
                                 "  parent_id = ?, position = ?,"
                                 "  updated_at = ?, updated_by = ?, revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(DocumentSql::nullableText(newParentId));
    query.addBindValue(position);
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(pageId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not move the page:\n%1").arg(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

QString DocumentPageMoves::errorString() const
{
    return m_errorString;
}
