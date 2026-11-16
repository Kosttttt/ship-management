#pragma once

#include "DocumentTestSupport.h"

#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentHistoryReader.h"
#include "modules/documents/data/DocumentImageStore.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/service/DocumentPublisher.h"
#include "modules/documents/service/DocumentRestorer.h"

// The fixture the step 2 suites share (document-history-spec.md §7): history,
// restore and the two history screens all need the same stack built on a
// temporary database.
//
// Its own header rather than more in DocumentTestSupport.h, so that step 1's
// suites do not start depending on step 2's classes to compile.
namespace DocumentHistoryTestSupport {

// Everything one of these tests needs, built once instead of in every case.
// Held by value and never copied, because each piece below holds a reference
// to the pieces above it.
struct Stack {
    QSqlDatabase            db;
    VesselRepository        vessels;
    DocumentSpaceRepository spaces;
    DocumentPageRepository  pages;
    DocumentRevisionStore   revisions;
    DocumentPageRemovals    removals;
    DocumentPublisher       publisher;
    DocumentHistoryReader   history;
    DocumentRestorer        restorer;
    DocumentImageStore      images;

    Stack(QSqlDatabase database, const InstallationContext& installation)
        : db(database)
        , vessels(db, installation)
        , spaces(db, installation, vessels)
        , pages(db, spaces)
        , revisions(db, installation.nodeId())
        , removals(db, pages)
        , publisher(db, spaces, pages, removals, revisions)
        , history(db, spaces, pages)
        , restorer(pages, history)
        , images(db, installation.nodeId())
    {
    }

    Stack(const Stack&)            = delete;
    Stack& operator=(const Stack&) = delete;
};

inline QString paragraph(const QString& text)
{
    return QStringLiteral("<p>%1</p>").arg(text);
}

// Publishes one Collection page three times, so there is a history to look
// at: Issue 1, Issue 2, Issue 3. Returns the page's id, or an empty string
// when something went wrong, which every caller checks.
inline QString publishThreeTimes(Stack& s, const QString& spaceId, const QString& title)
{
    QString pageId;
    if (!s.pages.create(spaceId, QString(), title, &pageId)) {
        return QString();
    }
    for (int number = 1; number <= 3; ++number) {
        const QString note = QStringLiteral("Issue %1").arg(number);
        if (!s.pages.saveDraft(pageId, title, paragraph(note))
            || !s.publisher.publishPage(pageId, note)) {
            return QString();
        }
    }
    return pageId;
}

} // namespace DocumentHistoryTestSupport
