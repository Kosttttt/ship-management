#include "modules/documents/data/DocumentPageRemovals.h"

#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

DocumentPageRemovals::DocumentPageRemovals(QSqlDatabase&           database,
                                           DocumentPageRepository& pages)
    : m_database(database)
    , m_pages(pages)
{
}

bool DocumentPageRemovals::setWithdrawPending(const QString& pageId, bool pending)
{
    m_errorString.clear();

    if (!m_pages.mayWrite(pageId, &m_errorString)) { // the §4.3 write rule, asked once
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE document_page SET"
                                 "  withdraw_pending = ?,"
                                 "  updated_at = ?, updated_by = ?, revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(pending ? 1 : 0);
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(pageId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not mark the page:\n%1").arg(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DocumentPageRemovals::descendantIds(const QString& pageId, QStringList* ids)
{
    ids->clear();
    ids->append(pageId);

    // Breadth-first over the live children, which needs no recursive SQL and
    // no assumption about how deep a manual goes.
    for (int index = 0; index < ids->size(); ++index) {
        QSqlQuery children(m_database);
        children.prepare(QStringLiteral(
            "SELECT id FROM document_page WHERE parent_id = ? AND is_deleted = 0"));
        children.addBindValue(ids->at(index));

        if (!children.exec()) {
            m_errorString = QStringLiteral("Could not read the page's children:\n%1")
                                .arg(children.lastError().text());
            return false;
        }
        while (children.next()) {
            ids->append(children.value(0).toString());
        }
    }
    return true;
}

bool DocumentPageRemovals::countDescendants(const QString& pageId, int* count)
{
    m_errorString.clear();

    QStringList ids;
    if (!descendantIds(pageId, &ids)) {
        return false;
    }
    *count = ids.size() - 1; // the page itself is not its own descendant
    return true;
}

bool DocumentPageRemovals::markDeleted(const QStringList& pageIds)
{
    const QString now = DocumentSql::nowUtc();

    for (const QString& id : pageIds) {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("UPDATE document_page SET"
                                     "  is_deleted = 1, updated_at = ?, updated_by = ?,"
                                     "  revision = revision + 1"
                                     " WHERE id = ? AND is_deleted = 0"));
        query.addBindValue(now);
        query.addBindValue(DocumentSql::systemUser());
        query.addBindValue(id);

        if (!query.exec()) {
            m_errorString =
                QStringLiteral("Could not delete the page:\n%1").arg(query.lastError().text());
            return false;
        }
    }
    return true;
}

bool DocumentPageRemovals::softDelete(const QString& pageId)
{
    m_errorString.clear();

    if (!m_pages.mayWrite(pageId, &m_errorString)) { // the §4.3 write rule, asked once
        return false;
    }
    return markDeleted({pageId});
}

bool DocumentPageRemovals::softDeleteWithDescendants(const QString& pageId, int* deletedCount)
{
    m_errorString.clear();

    if (!m_pages.mayWrite(pageId, &m_errorString)) { // the §4.3 write rule, asked once
        return false;
    }

    QStringList ids;
    if (!descendantIds(pageId, &ids)) {
        return false;
    }
    if (!markDeleted(ids)) {
        return false;
    }
    if (deletedCount != nullptr) {
        *deletedCount = ids.size();
    }
    return true;
}

bool DocumentPageRemovals::softDeleteSpacePages(const QString& spaceId)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE document_page SET"
                                 "  is_deleted = 1, updated_at = ?, updated_by = ?,"
                                 "  revision = revision + 1"
                                 " WHERE space_id = ? AND is_deleted = 0"));
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(spaceId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not delete the space's pages:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}


bool DocumentPageRemovals::countSpacePages(const QString& spaceId, int* count)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM document_page WHERE space_id = ? AND is_deleted = 0"));
    query.addBindValue(spaceId);

    if (!query.exec() || !query.next()) {
        m_errorString = QStringLiteral("Could not count the space's pages:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    *count = query.value(0).toInt();
    return true;
}

QString DocumentPageRemovals::errorString() const
{
    return m_errorString;
}
