#include "modules/documents/data/DocumentHistoryReader.h"

#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

// Positional, read back positionally below. Anything new goes on the END.
//
// Spelled with the `r.` prefix because every query here joins: the same
// columns, named unambiguously, rather than a helper that glues a prefix on
// at run time.
const QString kPageRevisionColumns =
    QStringLiteral("r.id, r.page_id, r.number, r.space_revision_id, r.title, r.content,"
                   " r.note, r.created_at, r.created_by");

DocumentPageRevision pageRevisionFromRow(const QSqlQuery& query)
{
    DocumentPageRevision revision;
    revision.id              = query.value(0).toString();
    revision.pageId          = query.value(1).toString();
    revision.number          = query.value(2).toInt();
    revision.spaceRevisionId = query.value(3).toString();
    revision.title           = query.value(4).toString();
    revision.content         = query.value(5).toString();
    revision.note            = query.value(6).toString();
    revision.publishedAtUtc  = DocumentSql::fromUtcText(query.value(7).toString());
    revision.createdBy       = query.value(8).toString();
    return revision;
}

} // namespace

DocumentHistoryReader::DocumentHistoryReader(QSqlDatabase&            database,
                                             DocumentSpaceRepository& spaces,
                                             DocumentPageRepository&  pages)
    : m_database(database)
    , m_spaces(spaces)
    , m_pages(pages)
{
}

bool DocumentHistoryReader::spaceIsVisible(const QString& spaceId, bool* visible)
{
    *visible = false;

    std::optional<DocumentSpace> space;
    if (!m_spaces.findById(spaceId, &space)) {
        m_errorString = m_spaces.errorString();
        return false;
    }
    *visible = space.has_value();
    return true;
}

bool DocumentHistoryReader::listPageHistory(const QString&            pageId,
                                            QList<PageHistoryEntry>*  entries)
{
    m_errorString.clear();
    entries->clear();

    // Through the page repository, so a page in a space this installation
    // cannot see has no history here either.
    std::optional<DocumentPage> page;
    if (!m_pages.findById(pageId, &page)) {
        m_errorString = m_pages.errorString();
        return false;
    }
    if (!page.has_value()) {
        return true; // not visible, or gone: no history, and not an error
    }

    QSqlQuery query(m_database);
    // The manual's number comes from the space revision this page revision
    // belongs to; a Collection space's revisions have none, and NULL reads
    // back as 0, which is what "show the page's own number" means.
    query.prepare(QStringLiteral("SELECT %1, s.number, s.note FROM document_page_revision r"
                                 "  LEFT JOIN document_space_revision s"
                                 "    ON s.id = r.space_revision_id AND s.is_deleted = 0"
                                 " WHERE r.page_id = ? AND r.is_deleted = 0"
                                 " ORDER BY r.number DESC")
                      .arg(kPageRevisionColumns));
    query.addBindValue(pageId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the page's history:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        PageHistoryEntry entry;
        entry.revision     = pageRevisionFromRow(query);
        entry.manualNumber = query.value(9).toInt();
        entry.manualNote   = query.value(10).toString();
        entries->append(entry);
    }
    return true;
}

bool DocumentHistoryReader::findPageRevision(const QString&                       revisionId,
                                             std::optional<DocumentPageRevision>* revision)
{
    m_errorString.clear();
    revision->reset();

    QSqlQuery query(m_database);
    // The space is read in the same query, because what decides whether this
    // revision may be read is the space it is in — not whether its page is
    // still in the tree (§6: a withdrawn page's revisions stay readable).
    query.prepare(QStringLiteral("SELECT %1, p.space_id FROM document_page_revision r"
                                 "  JOIN document_page p ON p.id = r.page_id"
                                 " WHERE r.id = ? AND r.is_deleted = 0")
                      .arg(kPageRevisionColumns));
    query.addBindValue(revisionId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the revision:\n%1").arg(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        return true;
    }

    const DocumentPageRevision found   = pageRevisionFromRow(query);
    const QString              spaceId = query.value(9).toString();

    bool visible = false;
    if (!spaceIsVisible(spaceId, &visible)) {
        return false;
    }
    if (visible) {
        *revision = found;
    }
    return true;
}

bool DocumentHistoryReader::previousPageRevision(const DocumentPageRevision&          revision,
                                                 std::optional<DocumentPageRevision>* previous)
{
    m_errorString.clear();
    previous->reset();

    QList<PageHistoryEntry> history;
    if (!listPageHistory(revision.pageId, &history)) {
        return false;
    }

    // Newest first, so the one before this is the next entry along. Found by
    // walking the history rather than by number − 1, because a number is
    // never reused and a deleted revision would leave a hole.
    for (int index = 0; index < history.size(); ++index) {
        if (history.at(index).revision.id != revision.id) {
            continue;
        }
        if (index + 1 < history.size()) {
            *previous = history.at(index + 1).revision;
        }
        return true;
    }
    return true; // not in the history: nothing before it either
}

bool DocumentHistoryReader::listSpaceRevisions(const QString&                 spaceId,
                                               QList<DocumentSpaceRevision>*  revisions)
{
    m_errorString.clear();
    revisions->clear();

    bool visible = false;
    if (!spaceIsVisible(spaceId, &visible)) {
        return false;
    }
    if (!visible) {
        return true;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, space_id, number, note, created_at, created_by"
        " FROM document_space_revision WHERE space_id = ? AND is_deleted = 0"
        " ORDER BY number DESC"));
    query.addBindValue(spaceId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the manual's revisions:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        DocumentSpaceRevision revision;
        revision.id             = query.value(0).toString();
        revision.spaceId        = query.value(1).toString();
        revision.number         = query.value(2).toInt();
        revision.note           = query.value(3).toString();
        revision.publishedAtUtc = DocumentSql::fromUtcText(query.value(4).toString());
        revision.createdBy      = query.value(5).toString();
        revisions->append(revision);
    }
    return true;
}

bool DocumentHistoryReader::listRevisionPages(const QString&        spaceRevisionId,
                                              QList<RevisionPage>*  pages)
{
    m_errorString.clear();
    pages->clear();

    QSqlQuery spaceQuery(m_database);
    spaceQuery.prepare(QStringLiteral(
        "SELECT space_id FROM document_space_revision WHERE id = ? AND is_deleted = 0"));
    spaceQuery.addBindValue(spaceRevisionId);

    if (!spaceQuery.exec()) {
        m_errorString = QStringLiteral("Could not read the revision:\n%1")
                            .arg(spaceQuery.lastError().text());
        return false;
    }
    if (!spaceQuery.next()) {
        return true;
    }

    bool visible = false;
    if (!spaceIsVisible(spaceQuery.value(0).toString(), &visible)) {
        return false;
    }
    if (!visible) {
        return true;
    }

    QSqlQuery query(m_database);
    // By title, because a withdrawn page has left the tree and has no place
    // in it any more — and a Record of Revisions somebody has to find a page
    // in wants one order that is always the same.
    query.prepare(QStringLiteral(
        "SELECT r.id, r.page_id, r.title, r.number, p.is_deleted,"
        "  (SELECT MAX(number) FROM document_page_revision"
        "    WHERE page_id = r.page_id AND is_deleted = 0)"
        " FROM document_page_revision r"
        "  JOIN document_page p ON p.id = r.page_id"
        " WHERE r.space_revision_id = ? AND r.is_deleted = 0"
        " ORDER BY r.title COLLATE NOCASE"));
    query.addBindValue(spaceRevisionId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read what the revision covered:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        RevisionPage page;
        page.pageRevisionId = query.value(0).toString();
        page.pageId         = query.value(1).toString();
        page.title          = query.value(2).toString();

        const int  number      = query.value(3).toInt();
        const bool pageIsGone  = query.value(4).toInt() != 0;
        const int  highest     = query.value(5).toInt();

        if (pageIsGone && number == highest) {
            page.kind = RevisionPage::Kind::Withdrawn;
        } else if (number == 1) {
            page.kind = RevisionPage::Kind::New;
        } else {
            page.kind = RevisionPage::Kind::Changed;
        }
        pages->append(page);
    }
    return true;
}

QString DocumentHistoryReader::errorString() const
{
    return m_errorString;
}
