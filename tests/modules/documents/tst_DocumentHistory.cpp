#include "DocumentHistoryTestSupport.h"

#include "modules/documents/domain/DocumentDiff.h"

#include <QtTest>

using namespace DocumentTestSupport;
using namespace DocumentHistoryTestSupport;
using RevisionPageKind = DocumentHistoryReader::RevisionPage::Kind;

// Page history and the Record of Revisions — the reading half of step 2
// (document-history-spec.md §2, §3, §7). Restoring is a write with rules of
// its own and lives in tst_DocumentRestore.
//
// All on temporary in-memory databases; nothing can reach the real one
// (CLAUDE.md §10.1).
class TestDocumentHistory : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §2
    void aCollectionPagesHistoryComesBackNewestFirst();
    void aManualPagesHistoryCarriesTheManualRevisionNumber();
    void compareWithPreviousFindsTheRevisionBeforeIt();
    void compareWithPublishedMarksExactlyTheDraftsChanges();

    // §3
    void theRecordOfRevisionsMarksNewChangedAndWithdrawnPages();

    // §4.3, which history must not quietly undo
    void anotherVesselsHistoryIsNotReadable();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
};

void TestDocumentHistory::init()
{
    m_connectionName = QStringLiteral("dochist_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestDocumentHistory::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentHistory::aCollectionPagesHistoryComesBackNewestFirst()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    QString pageId;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));

    for (int number = 1; number <= 3; ++number) {
        QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Circular 01"),
                                  paragraph(QStringLiteral("Issue %1").arg(number))));
        QVERIFY2(s.publisher.publishPage(pageId, QStringLiteral("Issue %1").arg(number)),
                 qPrintable(s.publisher.errorString()));
    }

    QList<DocumentHistoryReader::PageHistoryEntry> history;
    QVERIFY2(s.history.listPageHistory(pageId, &history), qPrintable(s.history.errorString()));

    QCOMPARE(history.size(), 3);
    QCOMPARE(history.at(0).revision.number, 3);
    QCOMPARE(history.at(1).revision.number, 2);
    QCOMPARE(history.at(2).revision.number, 1);

    // The note, and who published it — SYSTEM until users exist, and read
    // anyway so the column is already there when they do (§2).
    QCOMPARE(history.at(0).revision.note, QStringLiteral("Issue 3"));
    QCOMPARE(history.at(0).revision.createdBy, QStringLiteral("SYSTEM"));
    QVERIFY(history.at(0).revision.publishedAtUtc.isValid());

    // Each revision keeps the content it was published with.
    QVERIFY(history.at(2).revision.content.contains(QStringLiteral("Issue 1")));
    QVERIFY(history.at(0).revision.content.contains(QStringLiteral("Issue 3")));

    // No manual revision in a Collection space, so no "Manual rev" column.
    for (const auto& entry : history) {
        QCOMPARE(entry.manualNumber, 0);
    }
}

void TestDocumentHistory::aManualPagesHistoryCarriesTheManualRevisionNumber()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual,
                                        QString(), &spaceId));
    QString chapter;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &chapter));
    QVERIFY(s.pages.saveDraft(chapter, QStringLiteral("Chapter 5"), paragraph(QStringLiteral("First"))));
    QVERIFY2(s.publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr),
             qPrintable(s.publisher.errorString()));

    QVERIFY(s.pages.saveDraft(chapter, QStringLiteral("Chapter 5"),
                              paragraph(QStringLiteral("Second"))));
    QVERIFY2(s.publisher.publishManual(spaceId, QStringLiteral("Fire drill frequency"), nullptr),
             qPrintable(s.publisher.errorString()));

    QList<DocumentHistoryReader::PageHistoryEntry> history;
    QVERIFY(s.history.listPageHistory(chapter, &history));

    QCOMPARE(history.size(), 2);
    // The page's own count, and the manual's — both, because "Rev 2 of this
    // page" and "Rev 2 of the manual" are different facts (§2).
    QCOMPARE(history.at(0).revision.number, 2);
    QCOMPARE(history.at(0).manualNumber, 2);
    QCOMPARE(history.at(1).revision.number, 1);
    QCOMPARE(history.at(1).manualNumber, 1);

    // The note of a manual's page comes from the manual's revision, because
    // that is where a manual's one statement of what changed is written
    // (§6.2 of the module spec). The page revision itself carries none, and
    // is not rewritten to pretend otherwise.
    QVERIFY(history.at(0).revision.note.isEmpty());
    QCOMPARE(history.at(0).manualNote, QStringLiteral("Fire drill frequency"));
    QCOMPARE(history.at(0).shownNote(), QStringLiteral("Fire drill frequency"));
    QCOMPARE(history.at(1).shownNote(), QStringLiteral("Initial issue"));
}

void TestDocumentHistory::compareWithPublishedMarksExactlyTheDraftsChanges()
{
    // §4.1 and §7's last line. The comparison itself is tested in
    // tst_DocumentDiff; what this checks is that the two sides handed to it
    // are the right ones — the page's draft against the revision in force,
    // and nothing else.
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    QString pageId;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));

    const QString published = QStringLiteral("<p>Sound the tanks before bunkering.</p>"
                                             "<p>Record the figures in the log.</p>");
    QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Circular 01"), published));
    QVERIFY(s.publisher.publishPage(pageId, QString()));

    // One word of the first paragraph, and the second left alone.
    const QString draft = QStringLiteral("<p>Sound the tanks after bunkering.</p>"
                                         "<p>Record the figures in the log.</p>");
    QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Circular 01"), draft));

    std::optional<DocumentPage> page;
    QVERIFY(s.pages.findById(pageId, &page));
    std::optional<DocumentPageRevision> inForce;
    QVERIFY(s.revisions.latestPageRevision(pageId, &inForce));
    QVERIFY(inForce.has_value());

    const DocumentDiff::Result result =
        DocumentDiff::compare(inForce->content, page->draftContent);

    QCOMPARE(result.countOf(DocumentDiff::BlockChange::Kind::Changed), 1);
    QCOMPARE(result.countOf(DocumentDiff::BlockChange::Kind::Added), 0);
    QCOMPARE(result.countOf(DocumentDiff::BlockChange::Kind::Removed), 0);
    QCOMPARE(result.countOf(DocumentDiff::BlockChange::Kind::Unchanged), 1);

    QString added;
    QString removed;
    for (const DocumentDiff::BlockChange& change : result.changes) {
        for (const DocumentDiff::Piece& piece : change.pieces) {
            if (piece.mark == DocumentDiff::Piece::Mark::Added) {
                added += piece.text;
            } else if (piece.mark == DocumentDiff::Piece::Mark::Removed) {
                removed += piece.text;
            }
        }
    }
    QCOMPARE(added.trimmed(), QStringLiteral("after"));
    QCOMPARE(removed.trimmed(), QStringLiteral("before"));
}

void TestDocumentHistory::compareWithPreviousFindsTheRevisionBeforeIt()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    QString pageId;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Circular 01"), &pageId));

    for (int number = 1; number <= 2; ++number) {
        QVERIFY(s.pages.saveDraft(pageId, QStringLiteral("Circular 01"),
                                  paragraph(QStringLiteral("Issue %1").arg(number))));
        QVERIFY(s.publisher.publishPage(pageId, QString()));
    }

    QList<DocumentHistoryReader::PageHistoryEntry> history;
    QVERIFY(s.history.listPageHistory(pageId, &history));

    std::optional<DocumentPageRevision> previous;
    QVERIFY(s.history.previousPageRevision(history.at(0).revision, &previous));
    QVERIFY(previous.has_value());
    QCOMPARE(previous->number, 1);

    // The first revision has nothing before it, which is what greys the
    // button rather than comparing it against an empty page.
    QVERIFY(s.history.previousPageRevision(history.at(1).revision, &previous));
    QVERIFY(!previous.has_value());
}

void TestDocumentHistory::theRecordOfRevisionsMarksNewChangedAndWithdrawnPages()
{
    // Step 1's own scenario, read back as §3's screen shows it: Rev 1 issues
    // three chapters, Rev 2 changes one, Rev 3 withdraws one.
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual,
                                        QString(), &spaceId));

    QString five;
    QString six;
    QString seven;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &five));
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &six));
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 7"), &seven));
    const QStringList titles{QStringLiteral("Chapter 5"), QStringLiteral("Chapter 6"),
                             QStringLiteral("Chapter 7")};
    const QStringList ids{five, six, seven};
    for (int index = 0; index < 3; ++index) {
        QVERIFY(s.pages.saveDraft(ids.at(index), titles.at(index),
                                  paragraph(QStringLiteral("First"))));
    }
    QVERIFY2(s.publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr),
             qPrintable(s.publisher.errorString()));

    QVERIFY(s.pages.saveDraft(six, QStringLiteral("Chapter 6"),
                              paragraph(QStringLiteral("Second"))));
    QVERIFY(s.publisher.publishManual(spaceId, QStringLiteral("Chapter 6 reworded"), nullptr));

    QVERIFY(s.removals.setWithdrawPending(seven, true));
    QVERIFY(s.publisher.publishManual(spaceId, QStringLiteral("Chapter 7 withdrawn"), nullptr));

    QList<DocumentSpaceRevision> spaceRevisions;
    QVERIFY2(s.history.listSpaceRevisions(spaceId, &spaceRevisions),
             qPrintable(s.history.errorString()));

    // Newest first (§3).
    QCOMPARE(spaceRevisions.size(), 3);
    QCOMPARE(spaceRevisions.at(0).number, 3);
    QCOMPARE(spaceRevisions.at(0).note, QStringLiteral("Chapter 7 withdrawn"));
    QCOMPARE(spaceRevisions.at(2).number, 1);

    // Rev 1: three pages, all new.
    QList<DocumentHistoryReader::RevisionPage> firstPages;
    QVERIFY(s.history.listRevisionPages(spaceRevisions.at(2).id, &firstPages));
    QCOMPARE(firstPages.size(), 3);
    for (const auto& page : firstPages) {
        QCOMPARE(page.kind, RevisionPageKind::New);
    }

    // Rev 2: only Chapter 6, and changed rather than new.
    QList<DocumentHistoryReader::RevisionPage> secondPages;
    QVERIFY(s.history.listRevisionPages(spaceRevisions.at(1).id, &secondPages));
    QCOMPARE(secondPages.size(), 1);
    QCOMPARE(secondPages.first().title, QStringLiteral("Chapter 6"));
    QCOMPARE(secondPages.first().kind, RevisionPageKind::Changed);

    // Rev 3: only Chapter 7, withdrawn — and the revision still holds what
    // it said, so "Rev 3 withdrew Chapter 7" stays answerable (§6.3).
    QList<DocumentHistoryReader::RevisionPage> thirdPages;
    QVERIFY(s.history.listRevisionPages(spaceRevisions.at(0).id, &thirdPages));
    QCOMPARE(thirdPages.size(), 1);
    QCOMPARE(thirdPages.first().title, QStringLiteral("Chapter 7"));
    QCOMPARE(thirdPages.first().kind, RevisionPageKind::Withdrawn);

    std::optional<DocumentPageRevision> withdrawn;
    QVERIFY(s.history.findPageRevision(thirdPages.first().pageRevisionId, &withdrawn));
    QVERIFY(withdrawn.has_value());
    QVERIFY(withdrawn->content.contains(QStringLiteral("First")));
}

void TestDocumentHistory::anotherVesselsHistoryIsNotReadable()
{
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, QStringLiteral("vessel-1"), QStringLiteral("V TRE"),
                        QStringLiteral("9123456"))
                 .isEmpty());
    QVERIFY(!seedVessel(db, QStringLiteral("vessel-2"), QStringLiteral("V DUE"),
                        QStringLiteral("9654321"))
                 .isEmpty());

    QString otherPage;
    QString otherRevision;
    QString otherSpace;
    {
        // The office makes the other vessel's space and publishes in it.
        Stack          office(db, officeContext());
        QList<DocumentSpace> spaces;
        QVERIFY(office.spaces.createCompanySpace(QStringLiteral("V DUE pages"),
                                                 SpaceKind::Collection, QString(), &otherSpace));
        // Made a vessel space directly, because createCompanySpace() makes
        // company ones and this test is about a vessel's own space.
        QSqlQuery claim(db);
        claim.prepare(QStringLiteral("UPDATE document_space SET vessel_id = ? WHERE id = ?"));
        claim.addBindValue(QStringLiteral("vessel-2"));
        claim.addBindValue(otherSpace);
        QVERIFY(claim.exec());

        QVERIFY(office.pages.create(otherSpace, QString(), QStringLiteral("Night orders"),
                                    &otherPage));
        QVERIFY(office.pages.saveDraft(otherPage, QStringLiteral("Night orders"),
                                       paragraph(QStringLiteral("Call me at any time."))));
        QVERIFY(office.publisher.publishPage(otherPage, QString()));

        QList<DocumentHistoryReader::PageHistoryEntry> history;
        QVERIFY(office.history.listPageHistory(otherPage, &history));
        QCOMPARE(history.size(), 1);
        otherRevision = history.first().revision.id;
    }

    // Vessel 1 cannot read any of it, by page id or by revision id — the
    // routes a screen would take (§4.3).
    Stack ship(db, vesselContext(QStringLiteral("vessel-1"), QStringLiteral("9123456")));

    QList<DocumentHistoryReader::PageHistoryEntry> history;
    QVERIFY(ship.history.listPageHistory(otherPage, &history));
    QVERIFY2(history.isEmpty(), "another vessel's page has no history here");

    std::optional<DocumentPageRevision> revision;
    QVERIFY(ship.history.findPageRevision(otherRevision, &revision));
    QVERIFY2(!revision.has_value(), "another vessel's revision is not readable");

    QList<DocumentSpaceRevision> spaceRevisions;
    QVERIFY(ship.history.listSpaceRevisions(otherSpace, &spaceRevisions));
    QVERIFY(spaceRevisions.isEmpty());
}

QTEST_MAIN(TestDocumentHistory)
#include "tst_DocumentHistory.moc"
