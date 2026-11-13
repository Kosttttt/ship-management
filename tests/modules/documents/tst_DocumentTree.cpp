#include "DocumentTestSupport.h"

#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentPageMoves.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentSpaceRepository.h"

#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §5 and §7: the page tree — creating pages,
// ordering them, and moving them to a new parent.
//
// The one that matters beyond the obvious is the last: a page moved under
// itself or under its own child would be cut off the tree entirely, invisible
// from the top and unreachable without editing the database.
class TestDocumentTree : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void topLevelAndChildPagesAreCreated();
    void theTreeComesBackParentsBeforeChildren();
    void moveUpAndMoveDownReorderSiblings();
    void movingAtTheEndsDoesNothing();
    void moveToReparentsThePage();
    void moveToTheTopOfTheTree();
    void aPageCannotBeMovedUnderItself();
    void aPageCannotBeMovedUnderItsOwnChild();
    void aPageCannotBeMovedIntoAnotherSpace();

    void aPageOfAReadOnlySpaceCannotBeCreatedOrMoved();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
    QString m_spaceId;
};

void TestDocumentTree::init()
{
    m_connectionName = QStringLiteral("doctree_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    QVERIFY(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &m_spaceId));
}

void TestDocumentTree::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentTree::topLevelAndChildPagesAreCreated()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);

    QString chapter;
    QVERIFY2(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &chapter),
             qPrintable(pages.errorString()));

    QString section;
    QVERIFY(pages.create(m_spaceId, chapter, QStringLiteral("5.1 Drills"), &section));

    QList<DocumentPage> tree;
    QVERIFY(pages.listTree(m_spaceId, &tree));
    QCOMPARE(tree.size(), 2);
    QVERIFY(tree.at(0).parentId.isEmpty()); // top of the tree
    QCOMPARE(tree.at(1).parentId, chapter);

    // A new page is a draft page: its title is the draft's, and it has no
    // revision until the manual is published (§4.2).
    QCOMPARE(tree.at(0).draftTitle, QStringLiteral("Chapter 5"));
    QVERIFY(tree.at(0).hasDraft());
}

void TestDocumentTree::theTreeComesBackParentsBeforeChildren()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);

    QString five;
    QString six;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &five));
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 6"), &six));

    QString fiveOne;
    QVERIFY(pages.create(m_spaceId, five, QStringLiteral("5.1"), &fiveOne));
    QVERIFY(pages.create(m_spaceId, fiveOne, QStringLiteral("5.1.1"), nullptr));
    QVERIFY(pages.create(m_spaceId, six, QStringLiteral("6.1"), nullptr));

    QList<DocumentPage> tree;
    QVERIFY(pages.listTree(m_spaceId, &tree));

    // The order a tree widget can be filled in with one pass: depth first,
    // so every parent has already been seen when its child arrives.
    QStringList titles;
    for (const DocumentPage& page : tree) {
        titles.append(page.draftTitle);
    }
    QCOMPARE(titles, QStringList({QStringLiteral("Chapter 5"), QStringLiteral("5.1"),
                                  QStringLiteral("5.1.1"), QStringLiteral("Chapter 6"),
                                  QStringLiteral("6.1")}));
}

void TestDocumentTree::moveUpAndMoveDownReorderSiblings()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString first;
    QString second;
    QString third;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("A"), &first));
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("B"), &second));
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("C"), &third));

    QVERIFY2(moves.moveUp(third), qPrintable(moves.errorString()));

    QList<DocumentPage> tree;
    QVERIFY(pages.listTree(m_spaceId, &tree));
    QCOMPARE(tree.at(0).draftTitle, QStringLiteral("A"));
    QCOMPARE(tree.at(1).draftTitle, QStringLiteral("C"));
    QCOMPARE(tree.at(2).draftTitle, QStringLiteral("B"));

    QVERIFY(moves.moveDown(third));
    QVERIFY(pages.listTree(m_spaceId, &tree));
    QCOMPARE(tree.at(1).draftTitle, QStringLiteral("B"));
    QCOMPARE(tree.at(2).draftTitle, QStringLiteral("C"));
}

void TestDocumentTree::movingAtTheEndsDoesNothing()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString only;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("A"), &only));

    // Success, not failure: the user asked for it to be as high as it can
    // go, and it is. A warning here would be telling somebody off for
    // nothing.
    QVERIFY(moves.moveUp(only));
    QVERIFY(moves.moveDown(only));
}

void TestDocumentTree::moveToReparentsThePage()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString five;
    QString six;
    QString section;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &five));
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 6"), &six));
    QVERIFY(pages.create(m_spaceId, five, QStringLiteral("Misfiled section"), &section));

    QVERIFY2(moves.moveTo(section, six), qPrintable(moves.errorString()));

    std::optional<DocumentPage> moved;
    QVERIFY(pages.findById(section, &moved));
    QCOMPARE(moved->parentId, six);
}

void TestDocumentTree::moveToTheTopOfTheTree()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString chapter;
    QString section;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &chapter));
    QVERIFY(pages.create(m_spaceId, chapter, QStringLiteral("Promote me"), &section));

    QVERIFY(moves.moveTo(section, QString()));

    std::optional<DocumentPage> moved;
    QVERIFY(pages.findById(section, &moved));
    QVERIFY(moved->parentId.isEmpty());
}

void TestDocumentTree::aPageCannotBeMovedUnderItself()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString page;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &page));

    QVERIFY(!moves.moveTo(page, page));
    QVERIFY2(moves.errorString().contains(QStringLiteral("itself")),
             qPrintable(moves.errorString()));

    std::optional<DocumentPage> unchanged;
    QVERIFY(pages.findById(page, &unchanged));
    QVERIFY(unchanged->parentId.isEmpty());
}

void TestDocumentTree::aPageCannotBeMovedUnderItsOwnChild()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString chapter;
    QString section;
    QString subsection;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &chapter));
    QVERIFY(pages.create(m_spaceId, chapter, QStringLiteral("5.1"), &section));
    QVERIFY(pages.create(m_spaceId, section, QStringLiteral("5.1.1"), &subsection));

    // Two levels down, so this is not caught by a simple "is it the direct
    // child" test — the whole branch has to be checked.
    QVERIFY(!moves.moveTo(chapter, subsection));

    QList<DocumentPage> tree;
    QVERIFY(pages.listTree(m_spaceId, &tree));
    QCOMPARE(tree.size(), 3); // all three still reachable from the top
}

void TestDocumentTree::aPageCannotBeMovedIntoAnotherSpace()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentPageRepository  pages(db, spaces);
    DocumentPageMoves       moves(db, pages);

    QString otherSpace;
    QVERIFY(spaces.createCompanySpace(QStringLiteral("Circulars"), SpaceKind::Collection,
                                      QString(), &otherSpace));

    QString here;
    QString there;
    QVERIFY(pages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &here));
    QVERIFY(pages.create(otherSpace, QString(), QStringLiteral("Circular 12"), &there));

    // A page of a manual moved into a collection would carry revisions that
    // mean nothing there, and vice versa.
    QVERIFY(!moves.moveTo(here, there));
    QVERIFY2(moves.errorString().contains(QStringLiteral("own space")),
             qPrintable(moves.errorString()));
}

void TestDocumentTree::aPageOfAReadOnlySpaceCannotBeCreatedOrMoved()
{
    QSqlDatabase            db = database();
    VesselRepository        officeVessels(db, officeContext());
    DocumentSpaceRepository office(db, officeContext(), officeVessels);
    DocumentPageRepository  officePages(db, office);

    QString page;
    QVERIFY(officePages.create(m_spaceId, QString(), QStringLiteral("Chapter 5"), &page));

    const QString vesselId =
        seedVessel(db, QStringLiteral("v1"), QStringLiteral("V UNO"), QStringLiteral("9074729"));
    const InstallationContext context = vesselContext(vesselId, QStringLiteral("9074729"));

    VesselRepository        vessels(db, context);
    DocumentSpaceRepository onBoardSpaces(db, context, vessels);
    DocumentPageRepository  onBoardPages(db, onBoardSpaces);
    DocumentPageMoves       onBoardMoves(db, onBoardPages);

    // §4.3, reaching the pages as well as the space: a company manual is
    // read-only on a ship, so nothing in it can be added, moved or changed.
    QVERIFY(!onBoardPages.create(m_spaceId, QString(), QStringLiteral("Our own chapter"),
                                 nullptr));
    QVERIFY(!onBoardMoves.moveUp(page));
    QVERIFY(!onBoardPages.saveDraft(page, QStringLiteral("Rewritten"), QString()));

    // But it is readable, which is the point of having it on board at all.
    std::optional<DocumentPage> readable;
    QVERIFY(onBoardPages.findById(page, &readable));
    QVERIFY(readable.has_value());
}

QTEST_MAIN(TestDocumentTree)
#include "tst_DocumentTree.moc"
