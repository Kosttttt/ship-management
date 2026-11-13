#include "DocumentTestSupport.h"

#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/data/DocumentVesselSpaces.h"

#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §4.3 and §7: who sees and may change which
// space, and the vessel spaces that are created for themselves.
//
// The access rules are the ones that matter most. A screen that forgot them
// would let a ship issue its own revision of the company's SMS, or show one
// vessel another's paperwork — which is why they are tested here, in the
// repository, with no widget in sight (CLAUDE.md §3).
class TestDocumentSpaces : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theOfficeCreatesAndEditsCompanySpaces();
    void aVesselCannotCreateACompanySpace();
    void aVesselSeesCompanySpacesReadOnly();
    void aVesselMayEditItsOwnSpace();
    void aVesselNeverSeesAnotherVesselsSpace();

    void vesselSpacesAreCreatedOncePerActiveVessel();
    void runningTheSetupTwiceCreatesNoDuplicates();
    void anArchivedVesselsSpaceIsHiddenNotDeleted();
    void aVesselSpaceCannotBeDeleted();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
    QString m_ownVesselId;
    QString m_otherVesselId;
};

void TestDocumentSpaces::init()
{
    m_connectionName = QStringLiteral("docspaces_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    m_ownVesselId   = seedVessel(db, QStringLiteral("v1"), QStringLiteral("V UNO"),
                                 QStringLiteral("9074729"));
    m_otherVesselId = seedVessel(db, QStringLiteral("v2"), QStringLiteral("V TRE"),
                                 QStringLiteral("9074730"));
    QVERIFY(!m_ownVesselId.isEmpty());
    QVERIFY(!m_otherVesselId.isEmpty());
}

void TestDocumentSpaces::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentSpaces::theOfficeCreatesAndEditsCompanySpaces()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentVesselSpaces    setup(db, officeContext(), vessels);

    QString id;
    QVERIFY2(spaces.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual,
                                       QString(), &id),
             qPrintable(spaces.errorString()));
    QVERIFY(spaces.rename(id, QStringLiteral("SMS Manual, 2026")));

    QList<DocumentSpace> listed;
    QVERIFY(spaces.list(&listed));
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.first().name, QStringLiteral("SMS Manual, 2026"));
    QCOMPARE(listed.first().kind, SpaceKind::Manual);
    QVERIFY(!listed.first().isVesselSpace());
    QVERIFY(spaces.mayEdit(listed.first()));

    QVERIFY(spaces.softDelete(id));
    QVERIFY(spaces.list(&listed));
    QVERIFY(listed.isEmpty());
}

void TestDocumentSpaces::aVesselCannotCreateACompanySpace()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));
    DocumentSpaceRepository spaces(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")),
                                   vessels);

    QVERIFY(!spaces.createCompanySpace(QStringLiteral("Invented On Board"), SpaceKind::Manual,
                                       QString(), nullptr));
    QVERIFY2(spaces.errorString().contains(QStringLiteral("office")),
             qPrintable(spaces.errorString()));
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space")), 0);
}

void TestDocumentSpaces::aVesselSeesCompanySpacesReadOnly()
{
    QSqlDatabase            db = database();
    VesselRepository        officeVessels(db, officeContext());
    DocumentSpaceRepository office(db, officeContext(), officeVessels);

    QString companyId;
    QVERIFY(office.createCompanySpace(QStringLiteral("SMS Manual"), SpaceKind::Manual, QString(),
                                      &companyId));

    const InstallationContext onBoardContext =
        vesselContext(m_ownVesselId, QStringLiteral("9074729"));
    VesselRepository        onBoardVessels(db, onBoardContext);
    DocumentSpaceRepository onBoard(db, onBoardContext, onBoardVessels);

    // Visible: a ship has to be able to read the company's SMS.
    std::optional<DocumentSpace> company;
    QVERIFY(onBoard.findById(companyId, &company));
    QVERIFY2(company.has_value(), "a vessel must be able to read company spaces");

    // Read-only: it will arrive from the office by package in step 6, and
    // until then nothing on board may change it (§4.3).
    QVERIFY(!onBoard.mayEdit(*company));
    QVERIFY(!onBoard.rename(companyId, QStringLiteral("Renamed On Board")));
    QVERIFY(!onBoard.softDelete(companyId));

    // And the stored row is untouched, not merely the call refused.
    std::optional<DocumentSpace> reread;
    QVERIFY(office.findById(companyId, &reread));
    QCOMPARE(reread->name, QStringLiteral("SMS Manual"));
}

void TestDocumentSpaces::aVesselMayEditItsOwnSpace()
{
    QSqlDatabase               db = database();
    const InstallationContext  context = vesselContext(m_ownVesselId, QStringLiteral("9074729"));
    VesselRepository           vessels(db, context);
    DocumentSpaceRepository    spaces(db, context, vessels);
    DocumentVesselSpaces       setup(db, context, vessels);

    QVERIFY(setup.ensureAll(nullptr));

    QList<DocumentSpace> listed;
    QVERIFY(spaces.list(&listed));
    QCOMPARE(listed.size(), 1); // its own, and nothing else exists yet
    QVERIFY(listed.first().isVesselSpace());
    QCOMPARE(listed.first().vesselId, m_ownVesselId);

    // The other half of §4.3, and the half that makes the module usable at
    // sea: its own space is entirely the ship's.
    QVERIFY(spaces.mayEdit(listed.first()));
    QVERIFY2(spaces.rename(listed.first().id, QStringLiteral("V UNO — ship's files")),
             qPrintable(spaces.errorString()));
}

void TestDocumentSpaces::aVesselNeverSeesAnotherVesselsSpace()
{
    QSqlDatabase            db = database();
    VesselRepository        officeVessels(db, officeContext());
    DocumentSpaceRepository office(db, officeContext(), officeVessels);
    DocumentVesselSpaces    setup(db, officeContext(), officeVessels);

    int created = 0;
    QVERIFY(setup.ensureAll(&created));
    QCOMPARE(created, 2); // one each for V UNO and V TRE

    QString othersSpaceId;
    QList<DocumentSpace> all;
    QVERIFY(office.list(&all));
    for (const DocumentSpace& space : all) {
        if (space.vesselId == m_otherVesselId) {
            othersSpaceId = space.id;
        }
    }
    QVERIFY(!othersSpaceId.isEmpty());

    const InstallationContext context = vesselContext(m_ownVesselId, QStringLiteral("9074729"));
    VesselRepository          vessels(db, context);
    DocumentSpaceRepository   onBoard(db, context, vessels);

    QList<DocumentSpace> listed;
    QVERIFY(onBoard.list(&listed));
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.first().vesselId, m_ownVesselId);

    // Nor by id, which is the route a screen would take.
    std::optional<DocumentSpace> others;
    QVERIFY(onBoard.findById(othersSpaceId, &others));
    QVERIFY2(!others.has_value(), "another vessel's space must not be readable by id");
}

void TestDocumentSpaces::vesselSpacesAreCreatedOncePerActiveVessel()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentVesselSpaces    setup(db, officeContext(), vessels);

    int created = 0;
    QVERIFY(setup.ensureAll(&created));
    QCOMPARE(created, 2);

    QList<DocumentSpace> listed;
    QVERIFY(spaces.list(&listed));
    QCOMPARE(listed.size(), 2);
    for (const DocumentSpace& space : listed) {
        // Always a Collection (§2): a ship's own space is its circulars and
        // reports, not a controlled manual with revisions of the whole.
        QCOMPARE(space.kind, SpaceKind::Collection);
        QVERIFY(space.isVesselSpace());
    }
}

void TestDocumentSpaces::runningTheSetupTwiceCreatesNoDuplicates()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentVesselSpaces    setup(db, officeContext(), vessels);

    int first = 0;
    QVERIFY(setup.ensureAll(&first));
    QCOMPARE(first, 2);

    // §4.3: the screen calls this every time it opens, so this is not an
    // edge case — it is the ordinary path from the second time onwards.
    int second = 0;
    QVERIFY(setup.ensureAll(&second));
    QCOMPARE(second, 0);

    QCOMPARE(rowCountOf(db, QStringLiteral("document_space")), 2);
}

void TestDocumentSpaces::anArchivedVesselsSpaceIsHiddenNotDeleted()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentVesselSpaces    setup(db, officeContext(), vessels);

    QVERIFY(setup.ensureAll(nullptr));
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space")), 2);

    // Archive V TRE the way the vessel screen does.
    QVERIFY(vessels.archive(m_otherVesselId));

    QList<DocumentSpace> listed;
    QVERIFY(spaces.list(&listed));
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.first().vesselId, m_ownVesselId);

    // **Hidden, not deleted** (§4.3): the row is still there, so restoring
    // the vessel brings its pages back with it rather than leaving them
    // orphaned.
    QCOMPARE(rowCountOf(db, QStringLiteral("document_space")), 2);

    QVERIFY(vessels.restore(m_otherVesselId));
    QVERIFY(spaces.list(&listed));
    QCOMPARE(listed.size(), 2);
}

void TestDocumentSpaces::aVesselSpaceCannotBeDeleted()
{
    QSqlDatabase            db = database();
    VesselRepository        vessels(db, officeContext());
    DocumentSpaceRepository spaces(db, officeContext(), vessels);
    DocumentVesselSpaces    setup(db, officeContext(), vessels);

    QVERIFY(setup.ensureAll(nullptr));

    QList<DocumentSpace> listed;
    QVERIFY(spaces.list(&listed));
    QVERIFY(!listed.isEmpty());

    // Even at the office. It belongs to the vessel, and §4.3 settles what
    // happens to it: hidden when the vessel is archived.
    QVERIFY(!spaces.softDelete(listed.first().id));
    QVERIFY2(spaces.errorString().contains(QStringLiteral("archived")),
             qPrintable(spaces.errorString()));
}

QTEST_MAIN(TestDocumentSpaces)
#include "tst_DocumentSpaces.moc"
