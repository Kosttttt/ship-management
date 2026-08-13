#include "VesselTestSupport.h"

#include "core/VesselRepository.h"

#include <QtTest>

using namespace VesselTestSupport;

// vessel-crud-spec §8 items 1 and 2: the VESSEL-mode filter from CLAUDE.md §3,
// and the is_deleted exclusion, both applied inside the repository.
class TestVesselRepositoryScope : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listInOfficeModeReturnsEveryVessel();
    void listInVesselModeReturnsOnlyItsOwn();
    void findByIdInVesselModeRefusesAnotherVessel();
    void listExcludesDeletedRows();
    void findByIdExcludesDeletedRows();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      m_connectionName;
};

void TestVesselRepositoryScope::init()
{
    m_connectionName = QStringLiteral("scope_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestVesselRepositoryScope::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestVesselRepositoryScope::listInOfficeModeReturnsEveryVessel()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    QList<Vessel> vessels;
    QVERIFY2(repository.list(&vessels), qPrintable(repository.errorString()));
    QCOMPARE(vessels.size(), 2);
    QCOMPARE(vessels.at(0).name, QStringLiteral("MV Alpha")); // sorted by name
    QCOMPARE(vessels.at(1).name, QStringLiteral("MV Bravo"));
}

void TestVesselRepositoryScope::listInVesselModeReturnsOnlyItsOwn()
{
    // Two rows exist, as a future sync might leave behind. A VESSEL
    // installation must still see exactly one.
    QSqlDatabase  db    = database();
    const QString ownId = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));
    QCOMPARE(rowCountOf(db, QStringLiteral("vessel")), 2);

    const InstallationContext context = vesselContext(ownId, QStringLiteral("9074729"));
    VesselRepository          repository(db, context);

    QList<Vessel> vessels;
    QVERIFY2(repository.list(&vessels), qPrintable(repository.errorString()));
    QCOMPARE(vessels.size(), 1);
    QCOMPARE(vessels.at(0).id, ownId);
    QCOMPARE(vessels.at(0).name, QStringLiteral("MV Alpha"));
}

void TestVesselRepositoryScope::findByIdInVesselModeRefusesAnotherVessel()
{
    QSqlDatabase  db      = database();
    const QString ownId   = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    const QString otherId = seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));

    const InstallationContext context = vesselContext(ownId, QStringLiteral("9074729"));
    VesselRepository          repository(db, context);

    std::optional<Vessel> found;
    QVERIFY(repository.findById(otherId, &found));
    QVERIFY2(!found.has_value(), "a vessel installation must not see another ship's row");

    QVERIFY(repository.findById(ownId, &found));
    QVERIFY(found.has_value());
}

void TestVesselRepositoryScope::listExcludesDeletedRows()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    const QString goneId = seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));
    markDeleted(db, goneId);

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    QList<Vessel> vessels;
    QVERIFY(repository.list(&vessels));
    QCOMPARE(vessels.size(), 1);
    QCOMPARE(vessels.at(0).name, QStringLiteral("MV Alpha"));
}

void TestVesselRepositoryScope::findByIdExcludesDeletedRows()
{
    QSqlDatabase  db     = database();
    const QString goneId = seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));
    markDeleted(db, goneId);

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    std::optional<Vessel> found;
    QVERIFY(repository.findById(goneId, &found));
    QVERIFY(!found.has_value());
}

QTEST_MAIN(TestVesselRepositoryScope)
#include "tst_VesselRepositoryScope.moc"
