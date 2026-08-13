#include "VesselTestSupport.h"

#include "core/VesselRepository.h"

#include <QtTest>

using namespace VesselTestSupport;

// vessel-crud-spec §8 items 3 to 8: the defensive checks, IMO uniqueness, and
// the revision rule for updates.
class TestVesselRepositoryWrites : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createRejectedInVesselMode();
    void createRejectsEmptyName();
    void createRejectsInvalidImoNumber();
    void createRejectsDuplicateImoNumber();
    void createStoresAllFields();
    void createStoresBlankOptionalFieldsAsNull();

    void updateRejectedForAnotherVesselInVesselMode();
    void updateAllowsResavingItsOwnUnchangedImoNumber();
    void updateRejectsImoNumberBelongingToAnotherVessel();
    void updateIncrementsRevisionAndLeavesCreationAlone();
    void updateChangesEveryEditableField();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      m_connectionName;
};

void TestVesselRepositoryWrites::init()
{
    m_connectionName = QStringLiteral("writes_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestVesselRepositoryWrites::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestVesselRepositoryWrites::createRejectedInVesselMode()
{
    QSqlDatabase  db    = database();
    const QString ownId = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    const InstallationContext context = vesselContext(ownId, QStringLiteral("9074729"));
    VesselRepository          repository(db, context);

    QVERIFY(!repository.create(sampleVessel(QStringLiteral("MV Bravo"),
                                            QStringLiteral("9319466"))));
    QVERIFY(!repository.errorString().isEmpty());
    QCOMPARE(rowCountOf(db, QStringLiteral("vessel")), 1);
}

void TestVesselRepositoryWrites::createRejectsEmptyName()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    QVERIFY(!repository.create(sampleVessel(QStringLiteral("   "), QStringLiteral("9074729"))));
    QCOMPARE(rowCountOf(db, QStringLiteral("vessel")), 0);
}

void TestVesselRepositoryWrites::createRejectsInvalidImoNumber()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    // Correct length, wrong check digit.
    QVERIFY(!repository.create(sampleVessel(QStringLiteral("MV Alpha"),
                                            QStringLiteral("9074728"))));
    QCOMPARE(rowCountOf(db, QStringLiteral("vessel")), 0);
}

void TestVesselRepositoryWrites::createRejectsDuplicateImoNumber()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    QVERIFY(!repository.create(sampleVessel(QStringLiteral("MV Impostor"),
                                            QStringLiteral("9074729"))));

    // The friendly message, not the raw SQLite constraint text. The database's
    // UNIQUE constraint stays as a backstop, but it must not be the path the
    // user hears about (vessel-crud-spec §6).
    QVERIFY(repository.errorString().contains(QStringLiteral("already belongs")));
    QVERIFY2(!repository.errorString().contains(QStringLiteral("UNIQUE"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
    QCOMPARE(rowCountOf(db, QStringLiteral("vessel")), 1);
}

void TestVesselRepositoryWrites::createStoresAllFields()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    QString newId;
    QVERIFY2(repository.create(sampleVessel(QStringLiteral("MV Alpha"),
                                            QStringLiteral("IMO 9074729")),
                               &newId),
             qPrintable(repository.errorString()));
    QVERIFY(!newId.isEmpty());

    std::optional<Vessel> stored;
    QVERIFY(repository.findById(newId, &stored));
    QVERIFY(stored.has_value());

    QCOMPARE(stored->name, QStringLiteral("MV Alpha"));
    QCOMPARE(stored->imoNumber, QStringLiteral("9074729")); // prefix stripped
    QCOMPARE(stored->callSign, QStringLiteral("ABCD"));
    QCOMPARE(stored->grossTonnage, 51000);
    QCOMPARE(stored->portOfRegistry, QStringLiteral("Limassol"));
    QCOMPARE(stored->flagState, QStringLiteral("Cyprus"));

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT created_by, updated_by, origin_node, revision,"
                                      " is_deleted FROM vessel")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(1).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(2).toString(), QStringLiteral("OFFICE"));
    QCOMPARE(query.value(3).toInt(), 1);
    QCOMPARE(query.value(4).toInt(), 0);
}

void TestVesselRepositoryWrites::createStoresBlankOptionalFieldsAsNull()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    Vessel bare;
    bare.name      = QStringLiteral("MV Bare");
    bare.imoNumber = QStringLiteral("9074729");

    QString newId;
    QVERIFY2(repository.create(bare, &newId), qPrintable(repository.errorString()));

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT call_sign IS NULL, gross_tonnage IS NULL, port_of_registry IS NULL,"
        " flag_state IS NULL FROM vessel")));
    QVERIFY(query.next());
    for (int column = 0; column < 4; ++column) {
        QVERIFY2(query.value(column).toInt() == 1, "a blank optional field should store as NULL");
    }

    // ...and reads back as an empty value, not as something odd.
    std::optional<Vessel> stored;
    QVERIFY(repository.findById(newId, &stored));
    QVERIFY(stored.has_value());
    QVERIFY(stored->callSign.isEmpty());
    QCOMPARE(stored->grossTonnage, 0);
}

void TestVesselRepositoryWrites::updateRejectedForAnotherVesselInVesselMode()
{
    QSqlDatabase  db      = database();
    const QString ownId   = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    const QString otherId = seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));

    const InstallationContext context = vesselContext(ownId, QStringLiteral("9074729"));
    VesselRepository          repository(db, context);

    Vessel other = sampleVessel(QStringLiteral("MV Hijacked"), QStringLiteral("9319466"));
    other.id     = otherId;

    QVERIFY(!repository.update(other));
    QVERIFY(repository.errorString().contains(QStringLiteral("own vessel")));

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT name FROM vessel WHERE id = ?"));
    query.addBindValue(otherId);
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("MV Bravo")); // untouched
}

void TestVesselRepositoryWrites::updateAllowsResavingItsOwnUnchangedImoNumber()
{
    // Without excluding the row's own id from the uniqueness check, every edit
    // that left the IMO field alone would fail against itself.
    QSqlDatabase  db = database();
    const QString id = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    Vessel edited = sampleVessel(QStringLiteral("MV Alpha Renamed"), QStringLiteral("9074729"));
    edited.id     = id;

    QVERIFY2(repository.update(edited), qPrintable(repository.errorString()));

    std::optional<Vessel> stored;
    QVERIFY(repository.findById(id, &stored));
    QCOMPARE(stored->name, QStringLiteral("MV Alpha Renamed"));
    QCOMPARE(stored->imoNumber, QStringLiteral("9074729"));
}

void TestVesselRepositoryWrites::updateRejectsImoNumberBelongingToAnotherVessel()
{
    QSqlDatabase  db      = database();
    const QString alphaId = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(db, QStringLiteral("MV Bravo"), QStringLiteral("9319466"));

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    Vessel edited = sampleVessel(QStringLiteral("MV Alpha"), QStringLiteral("9319466"));
    edited.id     = alphaId;

    QVERIFY(!repository.update(edited));
    QVERIFY(repository.errorString().contains(QStringLiteral("already belongs")));

    std::optional<Vessel> stored;
    QVERIFY(repository.findById(alphaId, &stored));
    QCOMPARE(stored->imoNumber, QStringLiteral("9074729")); // unchanged
}

void TestVesselRepositoryWrites::updateIncrementsRevisionAndLeavesCreationAlone()
{
    QSqlDatabase  db = database();
    const QString id = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    QSqlQuery before(db);
    QVERIFY(before.exec(QStringLiteral("SELECT revision, created_at, created_by FROM vessel")));
    QVERIFY(before.next());
    const int     revisionBefore  = before.value(0).toInt();
    const QString createdAtBefore = before.value(1).toString();
    const QString createdByBefore = before.value(2).toString();
    QCOMPARE(revisionBefore, 1);

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    Vessel edited = sampleVessel(QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    edited.id     = id;
    QVERIFY2(repository.update(edited), qPrintable(repository.errorString()));

    QSqlQuery after(db);
    QVERIFY(after.exec(QStringLiteral(
        "SELECT revision, created_at, created_by, updated_by FROM vessel")));
    QVERIFY(after.next());

    QCOMPARE(after.value(0).toInt(), revisionBefore + 1);
    QCOMPARE(after.value(1).toString(), createdAtBefore); // creation untouched
    QCOMPARE(after.value(2).toString(), createdByBefore);
    QCOMPARE(after.value(3).toString(), QStringLiteral("SYSTEM"));

    // A second save bumps it again, rather than sticking at 2.
    QVERIFY(repository.update(edited));
    QSqlQuery third(db);
    QVERIFY(third.exec(QStringLiteral("SELECT revision FROM vessel")));
    QVERIFY(third.next());
    QCOMPARE(third.value(0).toInt(), revisionBefore + 2);
}

void TestVesselRepositoryWrites::updateChangesEveryEditableField()
{
    QSqlDatabase  db = database();
    const QString id = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    const InstallationContext context = officeContext();
    VesselRepository          repository(db, context);

    Vessel edited;
    edited.id             = id;
    edited.name           = QStringLiteral("MV Renamed");
    edited.imoNumber      = QStringLiteral("9319466"); // the IMO number is editable (§5)
    edited.callSign       = QStringLiteral("ZZZZ");
    edited.grossTonnage   = 12345;
    edited.portOfRegistry = QStringLiteral("Valletta");
    edited.flagState      = QStringLiteral("Malta");

    QVERIFY2(repository.update(edited), qPrintable(repository.errorString()));

    std::optional<Vessel> stored;
    QVERIFY(repository.findById(id, &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->name, QStringLiteral("MV Renamed"));
    QCOMPARE(stored->imoNumber, QStringLiteral("9319466"));
    QCOMPARE(stored->callSign, QStringLiteral("ZZZZ"));
    QCOMPARE(stored->grossTonnage, 12345);
    QCOMPARE(stored->portOfRegistry, QStringLiteral("Valletta"));
    QCOMPARE(stored->flagState, QStringLiteral("Malta"));
}

QTEST_MAIN(TestVesselRepositoryWrites)
#include "tst_VesselRepositoryWrites.moc"
