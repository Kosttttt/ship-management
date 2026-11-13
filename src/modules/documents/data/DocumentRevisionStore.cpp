#include "modules/documents/data/DocumentRevisionStore.h"

#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// Positional, and read back positionally below. Anything new goes on the END
// of each list, never into the middle.
const QString kPageColumns = QStringLiteral(
    "id, page_id, number, space_revision_id, title, content, note, created_at");

const QString kSpaceColumns = QStringLiteral("id, space_id, number, note, created_at");

DocumentPageRevision pageRevisionFromRow(const QSqlQuery& query)
{
    DocumentPageRevision revision;
    revision.id     = query.value(0).toString();
    revision.pageId = query.value(1).toString();
    revision.number = query.value(2).toInt();
    // NULL reads back empty, which is what a Collection space's revision has.
    revision.spaceRevisionId = query.value(3).toString();
    revision.title           = query.value(4).toString();
    revision.content         = query.value(5).toString();
    revision.note            = query.value(6).toString();
    revision.publishedAtUtc  = DocumentSql::fromUtcText(query.value(7).toString());
    return revision;
}

DocumentSpaceRevision spaceRevisionFromRow(const QSqlQuery& query)
{
    DocumentSpaceRevision revision;
    revision.id             = query.value(0).toString();
    revision.spaceId        = query.value(1).toString();
    revision.number         = query.value(2).toInt();
    revision.note           = query.value(3).toString();
    revision.publishedAtUtc = DocumentSql::fromUtcText(query.value(4).toString());
    return revision;
}

} // namespace

DocumentRevisionStore::DocumentRevisionStore(QSqlDatabase& database, const QString& nodeId)
    : m_database(database)
    , m_nodeId(nodeId)
{
}

bool DocumentRevisionStore::listPageRevisions(const QString&                 pageId,
                                              QList<DocumentPageRevision>*   revisions)
{
    m_errorString.clear();
    revisions->clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document_page_revision"
                                 " WHERE page_id = ? AND is_deleted = 0"
                                 " ORDER BY number DESC")
                      .arg(kPageColumns));
    query.addBindValue(pageId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the revisions:\n%1").arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        revisions->append(pageRevisionFromRow(query));
    }
    return true;
}

bool DocumentRevisionStore::latestPageRevision(const QString&                        pageId,
                                               std::optional<DocumentPageRevision>*  revision)
{
    revision->reset();

    QList<DocumentPageRevision> revisions;
    if (!listPageRevisions(pageId, &revisions)) {
        return false;
    }
    // Newest first, so the one in force is simply the first.
    if (!revisions.isEmpty()) {
        *revision = revisions.first();
    }
    return true;
}

bool DocumentRevisionStore::latestSpaceRevision(const QString&                         spaceId,
                                                std::optional<DocumentSpaceRevision>*  revision)
{
    m_errorString.clear();
    revision->reset();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document_space_revision"
                                 " WHERE space_id = ? AND is_deleted = 0"
                                 " ORDER BY number DESC LIMIT 1")
                      .arg(kSpaceColumns));
    query.addBindValue(spaceId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the manual's revisions:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    if (query.next()) {
        *revision = spaceRevisionFromRow(query);
    }
    return true;
}

bool DocumentRevisionStore::stateFor(const DocumentPage& page, bool spaceIsManual,
                                     DocumentPageState* state)
{
    m_errorString.clear();

    *state      = DocumentPageState();
    state->page = page;

    std::optional<DocumentPageRevision> latest;
    if (!latestPageRevision(page.id, &latest)) {
        return false;
    }
    if (!latest.has_value()) {
        return true; // never published: a draft page, which is a valid state
    }

    state->hasPublished = true;
    state->published    = *latest;

    if (!spaceIsManual || latest->spaceRevisionId.isEmpty()) {
        return true; // a Collection page shows its own number
    }

    // In a Manual space the page shows the *manual's* number — "Rev 8" — so
    // that every page of a manual agrees about which revision it is at.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT number FROM document_space_revision WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(latest->spaceRevisionId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the manual's revision number:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    if (query.next()) {
        state->manualNumber = query.value(0).toInt();
    }
    return true;
}

bool DocumentRevisionStore::nextPageNumber(const QString& pageId, int* number)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    // Deleted revisions counted, so a number is never reused: two revisions
    // claiming to be "Rev 3" of one page is not a thing a record can survive.
    query.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(number), 0) + 1 FROM document_page_revision WHERE page_id = ?"));
    query.addBindValue(pageId);

    if (!query.exec() || !query.next()) {
        m_errorString = QStringLiteral("Could not work out the next revision number:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    *number = query.value(0).toInt();
    return true;
}

bool DocumentRevisionStore::nextSpaceNumber(const QString& spaceId, int* number)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(number), 0) + 1 FROM document_space_revision WHERE space_id = ?"));
    query.addBindValue(spaceId);

    if (!query.exec() || !query.next()) {
        m_errorString = QStringLiteral("Could not work out the next revision number:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    *number = query.value(0).toInt();
    return true;
}

bool DocumentRevisionStore::insertSpaceRevision(const QString& spaceId, int number,
                                                const QString& note, QString* newId)
{
    m_errorString.clear();

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = DocumentSql::nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO document_space_revision"
        " (id, space_id, number, note,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(spaceId);
    query.addBindValue(number);
    query.addBindValue(DocumentSql::nullableText(note.trimmed()));
    query.addBindValue(now);
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(now);
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(m_nodeId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not record the manual's revision:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool DocumentRevisionStore::insertPageRevision(const QString& pageId, int number,
                                               const QString& spaceRevisionId,
                                               const QString& title, const QString& content,
                                               const QString& note, QString* newId)
{
    m_errorString.clear();

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = DocumentSql::nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO document_page_revision"
        " (id, page_id, number, space_revision_id, title, content, note,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(pageId);
    query.addBindValue(number);
    query.addBindValue(DocumentSql::nullableText(spaceRevisionId));
    query.addBindValue(title);
    query.addBindValue(content);
    query.addBindValue(DocumentSql::nullableText(note.trimmed()));
    query.addBindValue(now);
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(now);
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(m_nodeId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not publish the page:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

QString DocumentRevisionStore::errorString() const
{
    return m_errorString;
}
