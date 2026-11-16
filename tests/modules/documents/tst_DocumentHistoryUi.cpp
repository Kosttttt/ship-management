#include "DocumentHistoryTestSupport.h"
#include "modules/documents/ui/DocumentHistoryDialog.h"
#include "modules/documents/ui/DocumentRevisionsDialog.h"

#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QtTest>

using namespace DocumentTestSupport;
using namespace DocumentHistoryTestSupport;

// The two history screens themselves (document-history-spec.md §2, §3).
//
// The reader is tested in tst_DocumentHistory; this is about what reaches the
// table and the tree — the columns that are there, the order they are in, and
// that Restore… from the dialog really does write the draft. Those are the
// things the developer sees, and a passing repository test says nothing about
// them.
class TestDocumentHistoryUi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theHistoryTableShowsEveryRevisionNewestFirst();
    void aCollectionSpaceHasNoManualRevColumn();
    void restoreFromTheDialogWritesTheDraft();
    void restoreIsGreyedOnAReadOnlySpace();
    void theRecordOfRevisionsListsEachRevisionAndItsPages();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
};

namespace {

QStringList headersOf(const QTableWidget* table)
{
    QStringList headers;
    for (int column = 0; column < table->columnCount(); ++column) {
        headers.append(table->horizontalHeaderItem(column)->text());
    }
    return headers;
}

} // namespace

void TestDocumentHistoryUi::init()
{
    m_connectionName = QStringLiteral("dochistui_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestDocumentHistoryUi::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentHistoryUi::theHistoryTableShowsEveryRevisionNewestFirst()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual,
                                        QString(), &spaceId));
    QString chapter;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &chapter));
    for (int issue = 1; issue <= 2; ++issue) {
        QVERIFY(s.pages.saveDraft(chapter, QStringLiteral("Chapter 5"),
                                  paragraph(QStringLiteral("Issue %1").arg(issue))));
        QVERIFY2(s.publisher.publishManual(spaceId, QStringLiteral("Rev note %1").arg(issue),
                                           nullptr),
                 qPrintable(s.publisher.errorString()));
    }

    std::optional<DocumentPage> page;
    QVERIFY(s.pages.findById(chapter, &page));

    DocumentHistoryDialog dialog(s.history, s.restorer, s.images);
    QVERIFY2(dialog.showFor(*page, QStringLiteral("Chapter 5"), /*manualSpace=*/true,
                            /*editable=*/true),
             qPrintable(dialog.errorString()));

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("pageHistoryTable"));
    QVERIFY(table != nullptr);

    // §2's columns, in that order, with Manual rev because this is a manual.
    QCOMPARE(headersOf(table),
             QStringList({QStringLiteral("Rev"), QStringLiteral("Manual rev"),
                          QStringLiteral("Date"), QStringLiteral("By"),
                          QStringLiteral("Note")}));

    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2")); // newest first
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("1"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Rev 2"));
    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("SYSTEM"));
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("Rev note 2"));

    // The revision in force is selected, which is where a reader starts.
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 0);

    // Nothing before Rev 1, so there is nothing to compare it with.
    auto* previous =
        dialog.findChild<QPushButton*>(QStringLiteral("compareWithPreviousButton"));
    QVERIFY(previous != nullptr);
    QVERIFY(previous->isEnabled()); // Rev 2 is selected, and Rev 1 is behind it
    table->selectRow(1);
    QVERIFY(!previous->isEnabled());
}

void TestDocumentHistoryUi::aCollectionSpaceHasNoManualRevColumn()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    const QString pageId = publishThreeTimes(s, spaceId, QStringLiteral("Circular 01"));
    QVERIFY(!pageId.isEmpty());

    std::optional<DocumentPage> page;
    QVERIFY(s.pages.findById(pageId, &page));

    DocumentHistoryDialog dialog(s.history, s.restorer, s.images);
    QVERIFY(dialog.showFor(*page, QStringLiteral("Circular 01"), /*manualSpace=*/false,
                           /*editable=*/true));

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("pageHistoryTable"));
    QCOMPARE(headersOf(table),
             QStringList({QStringLiteral("Rev"), QStringLiteral("Date"), QStringLiteral("By"),
                          QStringLiteral("Note")}));
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("3"));
}

void TestDocumentHistoryUi::restoreFromTheDialogWritesTheDraft()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    const QString pageId = publishThreeTimes(s, spaceId, QStringLiteral("Circular 01"));
    QVERIFY(!pageId.isEmpty());

    std::optional<DocumentPage> page;
    QVERIFY(s.pages.findById(pageId, &page));
    QVERIFY2(!page->hasDraft(), "published, so nothing to confirm away");

    DocumentHistoryDialog dialog(s.history, s.restorer, s.images);
    QVERIFY(dialog.showFor(*page, QStringLiteral("Circular 01"), false, /*editable=*/true));

    // Rev 1, the oldest, which is the interesting one to put back.
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("pageHistoryTable"));
    table->selectRow(2);

    auto* restore = dialog.findChild<QPushButton*>(QStringLiteral("restoreRevisionButton"));
    QVERIFY(restore != nullptr);
    QVERIFY(restore->isEnabled());
    restore->click();

    QVERIFY2(dialog.didRestore(), "the screen is told to re-read the page");

    std::optional<DocumentPage> afterwards;
    QVERIFY(s.pages.findById(pageId, &afterwards));
    QVERIFY(afterwards->hasDraft());
    QVERIFY2(afterwards->draftContent.contains(QStringLiteral("Issue 1")),
             qPrintable(afterwards->draftContent));

    // And nothing was published: still three revisions, Rev 3 still in force.
    QCOMPARE(rowCountOf(s.db, QStringLiteral("document_page_revision")), 3);
    std::optional<DocumentPageRevision> inForce;
    QVERIFY(s.revisions.latestPageRevision(pageId, &inForce));
    QCOMPARE(inForce->number, 3);
}

void TestDocumentHistoryUi::restoreIsGreyedOnAReadOnlySpace()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                        QString(), &spaceId));
    const QString pageId = publishThreeTimes(s, spaceId, QStringLiteral("Circular 01"));
    QVERIFY(!pageId.isEmpty());

    std::optional<DocumentPage> page;
    QVERIFY(s.pages.findById(pageId, &page));

    DocumentHistoryDialog dialog(s.history, s.restorer, s.images);
    QVERIFY(dialog.showFor(*page, QStringLiteral("Circular 01"), false, /*editable=*/false));

    // Reading is allowed; restoring is not (§5). The service refuses it as
    // well — this is the courtesy, not the rule.
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("viewRevisionButton"))->isEnabled());
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("compareRevisionsButton"))
                ->isEnabled());
    QVERIFY(
        !dialog.findChild<QPushButton*>(QStringLiteral("restoreRevisionButton"))->isEnabled());
}

void TestDocumentHistoryUi::theRecordOfRevisionsListsEachRevisionAndItsPages()
{
    Stack s(database(), officeContext());

    QString spaceId;
    QVERIFY(s.spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual,
                                        QString(), &spaceId));

    QString five;
    QString six;
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 5"), &five));
    QVERIFY(s.pages.create(spaceId, QString(), QStringLiteral("Chapter 6"), &six));
    QVERIFY(s.pages.saveDraft(five, QStringLiteral("Chapter 5"),
                              paragraph(QStringLiteral("First"))));
    QVERIFY(s.pages.saveDraft(six, QStringLiteral("Chapter 6"),
                              paragraph(QStringLiteral("First"))));
    QVERIFY(s.publisher.publishManual(spaceId, QStringLiteral("Initial issue"), nullptr));

    QVERIFY(s.pages.saveDraft(six, QStringLiteral("Chapter 6"),
                              paragraph(QStringLiteral("Second"))));
    QVERIFY(s.publisher.publishManual(spaceId, QStringLiteral("Chapter 6 reworded"), nullptr));

    std::optional<DocumentSpace> space;
    QVERIFY(s.spaces.findById(spaceId, &space));

    DocumentRevisionsDialog dialog(s.history, s.images);
    QVERIFY2(dialog.showFor(*space), qPrintable(dialog.errorString()));

    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("recordOfRevisionsTree"));
    QVERIFY(tree != nullptr);

    // Newest first (§3), with what each revision covered under it.
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Rev 2"));
    QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Chapter 6 reworded"));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Chapter 6"));
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(1), QStringLiteral("changed"));

    QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Rev 1"));
    QCOMPARE(tree->topLevelItem(1)->childCount(), 2);
    QCOMPARE(tree->topLevelItem(1)->child(0)->text(1), QStringLiteral("new"));
    QCOMPARE(tree->topLevelItem(1)->child(1)->text(1), QStringLiteral("new"));

    // A Rev row is not a page, so there is nothing to open from it; a page
    // row is.
    auto* open = dialog.findChild<QPushButton*>(QStringLiteral("openRevisionPageButton"));
    auto* compare = dialog.findChild<QPushButton*>(QStringLiteral("compareRevisionPageButton"));
    QVERIFY(open != nullptr);
    QVERIFY(!open->isEnabled());

    tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    QVERIFY(open->isEnabled());
    QVERIFY2(compare->isEnabled(), "a changed page has a revision before it");

    // A new page has nothing before it, so Compare stays off.
    tree->setCurrentItem(tree->topLevelItem(1)->child(0));
    QVERIFY(open->isEnabled());
    QVERIFY(!compare->isEnabled());
}

QTEST_MAIN(TestDocumentHistoryUi)
#include "tst_DocumentHistoryUi.moc"
