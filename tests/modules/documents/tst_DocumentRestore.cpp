#include "DocumentHistoryTestSupport.h"

#include <QtTest>

using namespace DocumentTestSupport;
using namespace DocumentHistoryTestSupport;

// Restore (document-history-spec.md §5, §7).
//
// Split from tst_DocumentHistory, which reached 565 lines, by the rule in
// CLAUDE.md §9 — and along the seam the spec itself draws: that suite is
// about reading what has been published, and this one is about putting an old
// revision back, which is a write and has rules of its own.
//
// The rule those rules protect: **restoring never publishes.** An old
// revision becomes the draft, and it goes out the normal way or not at all.
class TestDocumentRestore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void restoreCopiesTheRevisionIntoTheDraftAndPublishesNothing();
    void aRestoredDraftPublishesAsANewRevision();
    void restoreIsRefusedOnAReadOnlySpace();
    void aWithdrawnPagesRevisionIsReadableButNotRestorable();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
};

void TestDocumentRestore::init()
{
    m_connectionName = QStringLiteral("docrestore_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestDocumentRestore::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentRestore::restoreCopiesTheRevisionIntoTheDraftAndPublishesNothing()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    QString pageId;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));

    QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Circular 01 — first"),
                              paragraph(QStringLiteral("Sound the tanks."))));
    QVERIFY(s.publisher.publishPage(pageId, QStringLiteral("First issue")));
    QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Circular 01 — second"),
                              paragraph(QStringLiteral("Sound the tanks twice."))));
    QVERIFY(s.publisher.publishPage(pageId, QStringLiteral("Second issue")));

    QList<DocumentHistoryReader::PageHistoryEntry> history;
    QVERIFY(s.history.listPageHistory(pageId, &history));
    const DocumentPageRevision first = history.at(1).revision;

    QVERIFY2(s.restorer.restoreIntoDraft(first.id), qPrintable(s.restorer.errorString()));

    // The draft is the old revision, title and content both (§5).
    std::optional<DocumentPage> page;
    QVERIFY(s.pages.findById(pageId, &page));
    QVERIFY(page->hasDraft());
    QCOMPARE(page->draftTitle, QStringLiteral("Circular 01 — first"));
    QCOMPARE(page->draftContent, first.content);

    // **Nothing was published.** Still two revisions, and Rev 2 is still what
    // is in force — restoring is an edit, not an issue (§5).
    QCOMPARE(rowCountOf(s.db, QStringLiteral("document_page_revision")), 2);
    std::optional<DocumentPageRevision> inForce;
    QVERIFY(s.revisions.latestPageRevision(pageId, &inForce));
    QCOMPARE(inForce->number, 2);
    QCOMPARE(inForce->title, QStringLiteral("Circular 01 — second"));
}

void TestDocumentRestore::aRestoredDraftPublishesAsANewRevision()
{
    // §7's last line: the restored draft goes out as the next revision, and
    // the old one it came from is not rewritten.
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    QString pageId;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));

    QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("First"), paragraph(QStringLiteral("One"))));
    QVERIFY(s.publisher.publishPage(pageId, QString()));
    QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Second"), paragraph(QStringLiteral("Two"))));
    QVERIFY(s.publisher.publishPage(pageId, QString()));

    QList<DocumentHistoryReader::PageHistoryEntry> history;
    QVERIFY(s.history.listPageHistory(pageId, &history));
    const QString firstRevisionId = history.at(1).revision.id;

    QVERIFY(s.restorer.restoreIntoDraft(firstRevisionId));
    QVERIFY2(s.publisher.publishPage(pageId, QStringLiteral("Reverted to the first issue")),
             qPrintable(s.publisher.errorString()));

    QVERIFY(s.history.listPageHistory(pageId, &history));
    QCOMPARE(history.size(), 3);
    QCOMPARE(history.at(0).revision.number, 3);
    QCOMPARE(history.at(0).revision.title, QStringLiteral("First"));
    QVERIFY(history.at(0).revision.content.contains(QStringLiteral("One")));

    // Rev 1 is untouched — same id, same number, same note. An immutable
    // table stayed immutable.
    QCOMPARE(history.at(2).revision.id, firstRevisionId);
    QCOMPARE(history.at(2).revision.number, 1);
    QCOMPARE(history.at(2).revision.title, QStringLiteral("First"));
}

void TestDocumentRestore::restoreIsRefusedOnAReadOnlySpace()
{
    // A vessel may read a company manual's history and compare its
    // revisions, and may not restore one — refused in the service, not only
    // by a greyed button (§5).
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, QStringLiteral("vessel-1"), QStringLiteral("V TRE"),
                        QStringLiteral("9123456"))
                 .isEmpty());

    QString companySpace;
    QString companyPage;
    QString revisionId;
    {
        Stack office(db, officeContext());
        QVERIFY(office.spaces.createCompanySpace(QStringLiteral("SMS Manual"),
                                                 SpaceKind::Collection, QString(),
                                                 &companySpace));
        QVERIFY(office.pages.create(companySpace, QString(), QStringLiteral("Chapter 5"),
                                    &companyPage));
        QVERIFY(office.pages.saveDraft(companyPage, QStringLiteral("Chapter 5"),
                                       paragraph(QStringLiteral("First"))));
        QVERIFY(office.publisher.publishPage(companyPage, QString()));
        QVERIFY(office.pages.saveDraft(companyPage, QStringLiteral("Chapter 5"),
                                       paragraph(QStringLiteral("Second"))));
        QVERIFY(office.publisher.publishPage(companyPage, QString()));

        QList<DocumentHistoryReader::PageHistoryEntry> history;
        QVERIFY(office.history.listPageHistory(companyPage, &history));
        revisionId = history.at(1).revision.id;
    }

    Stack ship(db, vesselContext(QStringLiteral("vessel-1"), QStringLiteral("9123456")));

    // Readable: the history is there, and so is the old revision.
    QList<DocumentHistoryReader::PageHistoryEntry> onBoard;
    QVERIFY(ship.history.listPageHistory(companyPage, &onBoard));
    QCOMPARE(onBoard.size(), 2);

    std::optional<DocumentPageRevision> readable;
    QVERIFY(ship.history.findPageRevision(revisionId, &readable));
    QVERIFY2(readable.has_value(), "a vessel may read a company manual's revisions");

    // Not restorable, and the draft stays empty.
    QVERIFY2(!ship.restorer.restoreIntoDraft(revisionId), "a vessel cannot restore here");
    QVERIFY(!ship.restorer.errorString().isEmpty());

    std::optional<DocumentPage> page;
    QVERIFY(ship.pages.findById(companyPage, &page));
    QVERIFY(page.has_value());
    QVERIFY2(!page->hasDraft(), "nothing was written");
}

void TestDocumentRestore::aWithdrawnPagesRevisionIsReadableButNotRestorable()
{
    // §6: a withdrawn page's history stays readable through the Record of
    // Revisions, and bringing it back is not in this step.
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual,
                                        QString(), &spaceId));
    QString chapter;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &chapter));
    QVERIFY(s.pages.saveDraft(chapter, QStringLiteral("Chapter 6"),
                              paragraph(QStringLiteral("Towing"))));
    QVERIFY(s.publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr));

    QVERIFY(s.removals.setWithdrawPending(chapter, true));
    QVERIFY(s.publisher.publishManual(spaceId, QStringLiteral("Chapter 6 withdrawn"), nullptr));

    QList<DocumentSpaceRevision> spaceRevisions;
    QVERIFY(s.history.listSpaceRevisions(spaceId, &spaceRevisions));
    QList<DocumentHistoryReader::RevisionPage> covered;
    QVERIFY(s.history.listRevisionPages(spaceRevisions.at(0).id, &covered));
    QCOMPARE(covered.size(), 1);

    // Readable.
    std::optional<DocumentPageRevision> revision;
    QVERIFY(s.history.findPageRevision(covered.first().pageRevisionId, &revision));
    QVERIFY(revision.has_value());
    QVERIFY(revision->content.contains(QStringLiteral("Towing")));

    // Not restorable: the page has left the tree, so there is nothing to
    // restore it into.
    QVERIFY(!s.restorer.restoreIntoDraft(covered.first().pageRevisionId));
    QVERIFY2(s.restorer.errorString().contains(QStringLiteral("tree")),
             qPrintable(s.restorer.errorString()));
}


QTEST_MAIN(TestDocumentRestore)
#include "tst_DocumentRestore.moc"
