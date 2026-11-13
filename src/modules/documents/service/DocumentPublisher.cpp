#include "modules/documents/service/DocumentPublisher.h"

#include "core/TransactionScope.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/data/DocumentSpaceRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <optional>

DocumentPublisher::DocumentPublisher(QSqlDatabase& database, DocumentSpaceRepository& spaces,
                                     DocumentPageRepository& pages,
                                     DocumentPageRemovals&   removals,
                                     DocumentRevisionStore&  revisions)
    : m_database(database)
    , m_spaces(spaces)
    , m_pages(pages)
    , m_removals(removals)
    , m_revisions(revisions)
{
}

bool DocumentPublisher::publishPage(const QString& pageId, const QString& note)
{
    m_errorString.clear();

    // The write rule first, so a vessel cannot publish into a company space
    // (§4.3). The repository owns that question; this asks it.
    if (!m_pages.mayWrite(pageId, &m_errorString)) {
        return false;
    }

    std::optional<DocumentPage> page;
    if (!m_pages.findById(pageId, &page) || !page.has_value()) {
        m_errorString = QStringLiteral("This page is no longer there.");
        return false;
    }

    std::optional<DocumentSpace> space;
    if (!m_spaces.findById(page->spaceId, &space) || !space.has_value()) {
        m_errorString = QStringLiteral("This page's space is no longer there.");
        return false;
    }
    if (space->isManual()) {
        m_errorString = QStringLiteral(
            "Pages of a manual are published with the manual's next revision, not one at a "
            "time.");
        return false;
    }
    if (!page->hasDraft()) {
        m_errorString = QStringLiteral("This page has no unpublished changes.");
        return false;
    }

    // Both rows or neither: a revision with the draft still sitting there
    // would show "unpublished changes" on a page that was just published.
    TransactionScope transaction(m_database);
    if (!transaction.isOpen()) {
        m_errorString = QStringLiteral("The page could not be published: no transaction.");
        return false;
    }

    int number = 0;
    if (!m_revisions.nextPageNumber(pageId, &number)) {
        m_errorString = m_revisions.errorString();
        return false;
    }

    if (!m_revisions.insertPageRevision(pageId, number, QString(), page->draftTitle,
                                        page->draftContent, note, nullptr)) {
        m_errorString = m_revisions.errorString();
        return false;
    }
    if (!m_pages.clearDraft(pageId)) {
        m_errorString = m_pages.errorString();
        return false;
    }

    if (!transaction.commit()) {
        m_errorString = QStringLiteral("The page could not be published:\n%1")
                            .arg(m_database.lastError().text());
        return false;
    }
    return true;
}

bool DocumentPublisher::pendingChanges(const QString& spaceId, QList<PendingChange>* changes)
{
    m_errorString.clear();
    changes->clear();

    QList<DocumentPage> pages;
    if (!m_pages.listTree(spaceId, &pages)) { // tree order, so it reads like the manual
        m_errorString = m_pages.errorString();
        return false;
    }

    for (const DocumentPage& page : pages) {
        std::optional<DocumentPageRevision> published;
        if (!m_revisions.latestPageRevision(page.id, &published)) {
            m_errorString = m_revisions.errorString();
            return false;
        }

        PendingChange change;
        change.pageId = page.id;

        if (page.withdrawPending && published.has_value()) {
            change.kind  = PendingChange::Kind::Withdrawn;
            change.title = published->title;
        } else if (!published.has_value()) {
            // Never published. A page with no draft and no revision is one
            // somebody created and never typed into; it still counts as new,
            // because publishing the manual is what brings it into force.
            change.kind  = PendingChange::Kind::New;
            change.title = page.draftTitle;
        } else if (page.hasDraft()) {
            change.kind  = PendingChange::Kind::Changed;
            change.title = page.draftTitle;
        } else {
            continue; // published and unchanged: keeps its old revision
        }

        changes->append(change);
    }
    return true;
}

bool DocumentPublisher::publishManual(const QString& spaceId, const QString& note, int* newNumber)
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
            "This space is managed at the office and cannot be published here.");
        return false;
    }
    if (!space->isManual()) {
        m_errorString = QStringLiteral(
            "A collection's pages are published one at a time, not as a whole.");
        return false;
    }
    if (note.trimmed().isEmpty()) {
        m_errorString = QStringLiteral(
            "A manual revision needs a note saying what changed.");
        return false;
    }

    QList<PendingChange> changes;
    if (!pendingChanges(spaceId, &changes)) {
        return false;
    }
    if (changes.isEmpty()) {
        m_errorString = QStringLiteral("Nothing has changed since the last revision.");
        return false;
    }

    // **One transaction for the whole manual** (§6.2). Everything below either
    // lands together or not at all: a manual whose Rev 8 covers three of its
    // four changed pages is worse than one still at Rev 7.
    TransactionScope transaction(m_database);
    if (!transaction.isOpen()) {
        m_errorString = QStringLiteral("The revision could not be published: no transaction.");
        return false;
    }

    int number = 0;
    if (!m_revisions.nextSpaceNumber(spaceId, &number)) {
        m_errorString = m_revisions.errorString();
        return false;
    }

    QString spaceRevisionId;
    if (!m_revisions.insertSpaceRevision(spaceId, number, note, &spaceRevisionId)) {
        m_errorString = m_revisions.errorString();
        return false;
    }

    for (const PendingChange& change : changes) {
        std::optional<DocumentPage> page;
        if (!m_pages.findById(change.pageId, &page) || !page.has_value()) {
            m_errorString = QStringLiteral("One of the pages disappeared while publishing.");
            return false;
        }

        std::optional<DocumentPageRevision> published;
        if (!m_revisions.latestPageRevision(page->id, &published)) {
            m_errorString = m_revisions.errorString();
            return false;
        }

        int pageNumber = 0;
        if (!m_revisions.nextPageNumber(page->id, &pageNumber)) {
            m_errorString = m_revisions.errorString();
            return false;
        }

        // A withdrawal records what was in force when it was withdrawn,
        // rather than nothing: the revision that removed a section should
        // still say what the section said.
        const bool    withdrawing = change.kind == PendingChange::Kind::Withdrawn;
        const QString title       = withdrawing ? published->title : page->draftTitle;
        const QString content     = withdrawing ? published->content : page->draftContent;

        if (!m_revisions.insertPageRevision(page->id, pageNumber, spaceRevisionId, title, content,
                                            QString(), nullptr)) {
            m_errorString = m_revisions.errorString();
            return false;
        }

        if (!m_pages.clearDraft(page->id)) {
            m_errorString = m_pages.errorString();
            return false;
        }

        if (withdrawing) {
            // Now it leaves the tree. The revision above is what it leaves
            // behind, which is how "Rev N withdrew this page" stays knowable.
            if (!m_removals.softDelete(page->id)) {
                m_errorString = m_removals.errorString();
                return false;
            }
        }
    }

    if (!transaction.commit()) {
        m_errorString = QStringLiteral("The revision could not be published:\n%1")
                            .arg(m_database.lastError().text());
        return false;
    }

    if (newNumber != nullptr) {
        *newNumber = number;
    }
    return true;
}

bool DocumentPublisher::listWithdrawnIn(const QString& spaceRevisionId, QStringList* titles)
{
    m_errorString.clear();
    titles->clear();

    QSqlQuery query(m_database);
    // The page revisions of this manual revision whose page has left the
    // tree. See the header for why this is read structurally rather than
    // from a marker.
    query.prepare(QStringLiteral(
        "SELECT r.title FROM document_page_revision r"
        "  JOIN document_page p ON p.id = r.page_id"
        " WHERE r.space_revision_id = ? AND r.is_deleted = 0 AND p.is_deleted = 1"
        " ORDER BY r.title COLLATE NOCASE"));
    query.addBindValue(spaceRevisionId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read what the revision withdrew:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        titles->append(query.value(0).toString());
    }
    return true;
}

QString DocumentPublisher::errorString() const
{
    return m_errorString;
}
