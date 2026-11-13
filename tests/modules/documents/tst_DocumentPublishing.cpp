#include "DocumentTestSupport.h"

#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/service/DocumentPublisher.h"

#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §6.2, §6.3 and §7: drafts becoming published
// revisions, one page at a time in a Collection and a whole manual at once in
// a Manual space, and withdrawal.
//
// This is the part of the module a ship's safety case rests on, so it is
// tested at the service layer with no window: what is in force, what changed,
// and the fact that a manual revision is all-or-nothing.
class TestDocumentPublishing : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Collection, page by page
    void aCollectionPageGoesFromDraftToRevisionOne();
    void editingAPublishedPageMakesRevisionTwo();
    void aCollectionPageWithNoChangesCannotBePublished();

    // Manual, the whole manual at once
    void aManualPageCannotBePublishedOnItsOwn();
    void aManualRevisionNeedsANote();
    void aManualRevisionListsAndPublishesOnlyTheChangedPages();
    void aFailedManualPublishLeavesNothingHalfWritten();
    void aManualWithNothingChangedCannotBePublished();

    // Withdrawal (§6.3)
    void aWithdrawnPageStaysUntilThePublishThatRemovesIt();
    void undoWithdrawBeforePublishRestoresThePage();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    int revisionCountOf(const QString& pageId);

    QString m_connectionName;
};

void TestDocumentPublishing::init()
{
    m_connectionName = QStringLiteral("docpub_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestDocumentPublishing::cleanup()
{
    closeDatabase(m_connectionName);
}

int TestDocumentPublishing::revisionCountOf(const QString& pageId)
{
    QSqlDatabase db = database();
    QSqlQuery    query(db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM document_page_revision WHERE page_id = ? AND is_deleted = 0"));
    query.addBindValue(pageId);
    if (!query.exec() || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void TestDocumentPublishing::aCollectionPageGoesFromDraftToRevisionOne()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                      QString(), &spaceId));

    QString pageId;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));
    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Circular 01 — Bunkering"),
                            QStringLiteral("<p>Sound the tanks before and after.</p>")));

    // Draft: typed, and not in force.
    std::optional<DocumentPageRevision> beforePublish;
    QVERIFY(revisions.latestPageRevision(pageId, &beforePublish));
    QVERIFY(!beforePublish.has_value());

    QVERIFY2(publisher.publishPage(pageId, QStringLiteral("First issue")),
             qPrintable(publisher.errorString()));

    std::optional<DocumentPageRevision> published;
    QVERIFY(revisions.latestPageRevision(pageId, &published));
    QVERIFY(published.has_value());
    QCOMPARE(published->number, 1);
    QCOMPARE(published->title, QStringLiteral("Circular 01 — Bunkering"));
    QVERIFY(published->content.contains(QStringLiteral("Sound the tanks")));

    // In a Collection there is no manual revision to belong to.
    QVERIFY(published->spaceRevisionId.isEmpty());

    // And the draft is gone, so the page no longer claims unpublished
    // changes (§6.2).
    std::optional<DocumentPage> afterPublish;
    QVERIFY(pages.findById(pageId, &afterPublish));
    QVERIFY(!afterPublish->hasDraft());
}

void TestDocumentPublishing::editingAPublishedPageMakesRevisionTwo()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                      QString(), &spaceId));

    QString pageId;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));
    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Circular 01"), QStringLiteral("<p>One</p>")));
    QVERIFY(publisher.publishPage(pageId, QString()));

    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Circular 01"), QStringLiteral("<p>Two</p>")));
    QVERIFY2(publisher.publishPage(pageId, QStringLiteral("Corrected the tank list")),
             qPrintable(publisher.errorString()));

    std::optional<DocumentPageRevision> current;
    QVERIFY(revisions.latestPageRevision(pageId, &current));
    QCOMPARE(current->number, 2);
    QVERIFY(current->content.contains(QStringLiteral("Two")));

    // Revision 1 is still there and still says what it said. Both tables are
    // immutable (§4.2), which is the only reason a history is worth keeping.
    QList<DocumentPageRevision> all;
    QVERIFY(revisions.listPageRevisions(pageId, &all));
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.first().number, 2); // newest first
    QCOMPARE(all.last().number, 1);
    QVERIFY(all.last().content.contains(QStringLiteral("One")));
}

void TestDocumentPublishing::aCollectionPageWithNoChangesCannotBePublished()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                      QString(), &spaceId));

    QString pageId;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));
    QVERIFY(publisher.publishPage(pageId, QString()));

    // A second revision identical to the first is noise in a document
    // register, and somebody has to read that register later.
    QVERIFY(!publisher.publishPage(pageId, QString()));
    QCOMPARE(revisionCountOf(pageId), 1);
}

void TestDocumentPublishing::aManualPageCannotBePublishedOnItsOwn()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));

    QString pageId;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &pageId));
    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Chapter 5"), QStringLiteral("<p>Text</p>")));

    // §6.2: a controlled manual is issued as a whole, so that a page's
    // revision number means something against the manual's.
    QVERIFY(!publisher.publishPage(pageId, QString()));
    QVERIFY2(publisher.errorString().contains(QStringLiteral("manual")),
             qPrintable(publisher.errorString()));
    QCOMPARE(revisionCountOf(pageId), 0);
}

void TestDocumentPublishing::aManualRevisionNeedsANote()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), nullptr));

    // Blank, and blank-looking: a note of three spaces is no note.
    QVERIFY(!publisher.publishManual(spaceId, QString(), nullptr));
    QVERIFY(!publisher.publishManual(spaceId, QStringLiteral("   "), nullptr));
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space_revision")), 0);

    QVERIFY2(publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr),
             qPrintable(publisher.errorString()));
}

void TestDocumentPublishing::aManualRevisionListsAndPublishesOnlyTheChangedPages()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));

    QString five;
    QString six;
    QString seven;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &five));
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &six));
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 7"), &seven));
    for (const QString& id : {five, six, seven}) {
        QVERIFY(pages.saveDraft(id, QStringLiteral("Chapter"), QStringLiteral("<p>First</p>")));
    }

    int firstNumber = 0;
    QVERIFY2(publisher.publishManual(spaceId, QStringLiteral("Initial issue"), &firstNumber),
             qPrintable(publisher.errorString()));
    QCOMPARE(firstNumber, 1);
    QCOMPARE(revisionCountOf(five), 1);
    QCOMPARE(revisionCountOf(six), 1);
    QCOMPARE(revisionCountOf(seven), 1);

    // Two of the three edited.
    QVERIFY(pages.saveDraft(five, QStringLiteral("Chapter 5 — Drills"),
                            QStringLiteral("<p>Second</p>")));
    QVERIFY(pages.saveDraft(six, QStringLiteral("Chapter 6 — Permits"),
                            QStringLiteral("<p>Second</p>")));

    QList<DocumentPublisher::PendingChange> changes;
    QVERIFY(publisher.pendingChanges(spaceId, &changes));
    QCOMPARE(changes.size(), 2);
    QCOMPARE(changes.at(0).pageId, five); // tree order, so it reads like the manual
    QCOMPARE(changes.at(1).pageId, six);
    QCOMPARE(changes.at(0).kind, DocumentPublisher::PendingChange::Kind::Changed);
    QCOMPARE(changes.at(0).title, QStringLiteral("Chapter 5 — Drills"));

    int secondNumber = 0;
    QVERIFY(publisher.publishManual(spaceId, QStringLiteral("Chapters 5 and 6 revised"),
                                    &secondNumber));
    QCOMPARE(secondNumber, 2);

    // Exactly the two changed pages gained a revision. **Chapter 7 keeps the
    // one it had**: the point of publishing a manual as a whole is not that
    // every page is reissued, it is that every page's revision is known.
    QCOMPARE(revisionCountOf(five), 2);
    QCOMPARE(revisionCountOf(six), 2);
    QCOMPARE(revisionCountOf(seven), 1);
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space_revision")), 2);

    // And Chapter 7 still reads as part of Rev 2, at the revision it was
    // issued in.
    DocumentPageState state;
    std::optional<DocumentPage> seventh;
    QVERIFY(pages.findById(seven, &seventh));
    QVERIFY(revisions.stateFor(*seventh, true, &state));
    QVERIFY(state.hasPublished);
    QCOMPARE(state.displayNumber(), 1);
}

void TestDocumentPublishing::aFailedManualPublishLeavesNothingHalfWritten()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));

    QString five;
    QString six;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &five));
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &six));
    QVERIFY(pages.saveDraft(five, QStringLiteral("Chapter 5"), QStringLiteral("<p>Five</p>")));
    QVERIFY(pages.saveDraft(six, QStringLiteral("Chapter 6"), QStringLiteral("<p>Six</p>")));

    // A failure in the middle, arranged rather than waited for: a trigger
    // that refuses the *second* page's revision. A full disk or a locked
    // database at that moment looks exactly like this from the publisher's
    // side, and this is the only way to be sure the rollback happens without
    // filling a disk.
    QSqlQuery trigger(db);
    QVERIFY2(trigger.exec(QStringLiteral(
                 "CREATE TRIGGER refuse_chapter_six"
                 " BEFORE INSERT ON document_page_revision"
                 " WHEN NEW.title = 'Chapter 6'"
                 " BEGIN SELECT RAISE(ABORT, 'simulated failure'); END")),
             qPrintable(trigger.lastError().text()));

    QVERIFY(!publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr));
    QVERIFY(!publisher.errorString().isEmpty());

    // **Nothing.** Not the manual's revision, not Chapter 5's revision that
    // had already been written, and not the cleared draft.
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space_revision")), 0);
    QCOMPARE(rowCountOf(db, QStringLiteral("document_page_revision")), 0);

    std::optional<DocumentPage> fifth;
    QVERIFY(pages.findById(five, &fifth));
    QVERIFY2(fifth->hasDraft(), "the draft must survive a failed publish");
    QCOMPARE(fifth->draftContent, QStringLiteral("<p>Five</p>"));

    // Remove the trigger, and the same publish now works — which proves the
    // failure above was the trigger and not something broken for good.
    QSqlQuery drop(db);
    QVERIFY(drop.exec(QStringLiteral("DROP TRIGGER refuse_chapter_six")));
    QVERIFY2(publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr),
             qPrintable(publisher.errorString()));
    QCOMPARE(rowCountOf(db, QStringLiteral("document_page_revision")), 2);
}

void TestDocumentPublishing::aManualWithNothingChangedCannotBePublished()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), nullptr));
    QVERIFY(publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr));

    QVERIFY(!publisher.publishManual(spaceId, QStringLiteral("Nothing, really"), nullptr));
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space_revision")), 1);
}

void TestDocumentPublishing::aWithdrawnPageStaysUntilThePublishThatRemovesIt()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));

    QString keep;
    QString withdraw;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &keep));
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &withdraw));
    QVERIFY(pages.saveDraft(keep, QStringLiteral("Chapter 5"), QStringLiteral("<p>Keep</p>")));
    QVERIFY(pages.saveDraft(withdraw, QStringLiteral("Chapter 6 — Towing"),
                            QStringLiteral("<p>Old practice</p>")));
    QVERIFY(publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr));

    QVERIFY(removals.setWithdrawPending(withdraw, true));

    // §6.3: **still there**, because until the revision is issued the ships
    // are working to the manual that has it. Marked, struck through on
    // screen, and in force.
    QList<DocumentPage> tree;
    QVERIFY(pages.listTree(spaceId, &tree));
    QCOMPARE(tree.size(), 2);

    QList<DocumentPublisher::PendingChange> changes;
    QVERIFY(publisher.pendingChanges(spaceId, &changes));
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.first().kind, DocumentPublisher::PendingChange::Kind::Withdrawn);
    // The published title, not the draft's: the dialog names the page as the
    // ships know it.
    QCOMPARE(changes.first().title, QStringLiteral("Chapter 6 — Towing"));

    int number = 0;
    QVERIFY2(publisher.publishManual(spaceId, QStringLiteral("Chapter 6 withdrawn"), &number),
             qPrintable(publisher.errorString()));
    QCOMPARE(number, 2);

    // Gone from the tree now.
    QVERIFY(pages.listTree(spaceId, &tree));
    QCOMPARE(tree.size(), 1);
    QCOMPARE(tree.first().id, keep);

    // And Rev 2 says it withdrew it, with the text it had when it went.
    std::optional<DocumentSpaceRevision> revision;
    QVERIFY(revisions.latestSpaceRevision(spaceId, &revision));
    QVERIFY(revision.has_value());
    QCOMPARE(revision->number, 2);

    QStringList withdrawn;
    QVERIFY(publisher.listWithdrawnIn(revision->id, &withdrawn));
    QCOMPARE(withdrawn, QStringList({QStringLiteral("Chapter 6 — Towing")}));

    QList<DocumentPageRevision> itsRevisions;
    QVERIFY(revisions.listPageRevisions(withdraw, &itsRevisions));
    QCOMPARE(itsRevisions.size(), 2);
    QVERIFY(itsRevisions.first().content.contains(QStringLiteral("Old practice")));
}

void TestDocumentPublishing::undoWithdrawBeforePublishRestoresThePage()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentRevisionStore   revisions(db, QStringLiteral("OFFICE"));
    DocumentPageRemovals    removals(db, pages);
    DocumentPublisher       publisher(db, spaces, pages, removals, revisions);

    QString spaceId;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &spaceId));

    QString pageId;
    QVERIFY(pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &pageId));
    QVERIFY(pages.saveDraft(pageId, QStringLiteral("Chapter 6"), QStringLiteral("<p>Text</p>")));
    QVERIFY(publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr));

    QVERIFY(removals.setWithdrawPending(pageId, true));
    QVERIFY(removals.setWithdrawPending(pageId, false));

    std::optional<DocumentPage> page;
    QVERIFY(pages.findById(pageId, &page));
    QVERIFY(!page->withdrawPending);

    // Nothing pending, so there is nothing to publish — the mark was the
    // only change, and taking it back leaves the manual as it was.
    QList<DocumentPublisher::PendingChange> changes;
    QVERIFY(publisher.pendingChanges(spaceId, &changes));
    QVERIFY(changes.isEmpty());
    QVERIFY(!publisher.publishManual(spaceId, QStringLiteral("Never mind"), nullptr));
}

QTEST_MAIN(TestDocumentPublishing)
#include "tst_DocumentPublishing.moc"
