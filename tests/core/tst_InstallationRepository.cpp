#include "core/InstallationRepository.h"
#include "core/MigrationRunner.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

// first-run-wizard-spec §10, items 2 to 5.
//
// Each test gets a private in-memory database with the real migrations applied
// — 001 and 002 as compiled into this test binary — so the constraints under
// test are the ones the application will actually run against.
class TestInstallationRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void freshDatabaseHasNoInstallation();
    void createsOfficeInstallation();
    void officeInstallationStoresNullVesselId();
    void createsVesselInstallation();
    void nodeIdCarriesTheImoNumber();
    void loadReturnsStoredOfficeRow();
    void loadReturnsStoredVesselRow();
    void auditColumnsArePopulated();
    void rejectsInvalidImoNumber();
    void rejectsEmptyVesselName();
    void acceptsImoNumberWithPrefix();
    void rollsBackVesselWhenInstallationInsertFails();
    void imoNumberIsUniqueAtTheDatabaseLevel();

private:
    QSqlDatabase database();
    int          rowCountOf(const QString& tableName);

    QString m_connectionName;
};

void TestInstallationRepository::init()
{
    m_connectionName = QStringLiteral("test_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(db.open(), "could not open an in-memory SQLite database");

    QSqlQuery pragma(db);
    QVERIFY(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")));

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
}

void TestInstallationRepository::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QSqlDatabase TestInstallationRepository::database()
{
    return QSqlDatabase::database(m_connectionName);
}

int TestInstallationRepository::rowCountOf(const QString& tableName)
{
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void TestInstallationRepository::freshDatabaseHasNoInstallation()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    std::optional<InstallationRecord> record;
    QVERIFY2(repository.load(&record), qPrintable(repository.errorString()));

    // "No row" is a successful answer, not an error.
    QVERIFY(!record.has_value());
    QCOMPARE(rowCountOf(QStringLiteral("installation")), 0);
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 0);
}

void TestInstallationRepository::createsOfficeInstallation()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY2(repository.createOfficeInstallation(&created), qPrintable(repository.errorString()));

    QCOMPARE(created.mode, InstallationMode::Office);
    QCOMPARE(created.nodeId, QStringLiteral("OFFICE"));
    QVERIFY(!created.id.isEmpty());
    QVERIFY(created.vesselId.isEmpty());

    QCOMPARE(rowCountOf(QStringLiteral("installation")), 1);
    // An OFFICE installation must not invent a vessel.
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 0);
}

void TestInstallationRepository::officeInstallationStoresNullVesselId()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(repository.createOfficeInstallation(&created));

    // A real NULL, not an empty string: migration 001's CHECK requires it.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT vessel_id IS NULL FROM installation")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestInstallationRepository::createsVesselInstallation()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY2(repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                 QStringLiteral("9074729"),
                                                 &created),
             qPrintable(repository.errorString()));

    QCOMPARE(created.mode, InstallationMode::Vessel);
    QCOMPARE(created.vesselName, QStringLiteral("MV Example"));
    QCOMPARE(created.vesselImoNumber, QStringLiteral("9074729"));
    QVERIFY(!created.vesselId.isEmpty());
    QVERIFY(created.vesselId != created.id);

    QCOMPARE(rowCountOf(QStringLiteral("installation")), 1);
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 1);

    // The installation row points at the vessel row that was just written.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT i.vessel_id, v.id FROM installation i"
                                      " JOIN vessel v ON v.id = i.vessel_id")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), created.vesselId);
}

void TestInstallationRepository::nodeIdCarriesTheImoNumber()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                QStringLiteral("9074729"),
                                                &created));

    // CLAUDE.md §3: node_id is 'VESSEL-<IMO>'.
    QCOMPARE(created.nodeId, QStringLiteral("VESSEL-9074729"));
}

void TestInstallationRepository::loadReturnsStoredOfficeRow()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(repository.createOfficeInstallation(&created));

    // A second repository stands in for the next launch of the application.
    InstallationRepository reopened(db);

    std::optional<InstallationRecord> loaded;
    QVERIFY2(reopened.load(&loaded), qPrintable(reopened.errorString()));
    QVERIFY(loaded.has_value());

    QCOMPARE(loaded->id, created.id);
    QCOMPARE(loaded->mode, InstallationMode::Office);
    QCOMPARE(loaded->nodeId, QStringLiteral("OFFICE"));
    QVERIFY(loaded->vesselName.isEmpty());

    const InstallationContext context(*loaded);
    QVERIFY(context.isConfigured());
    // OFFICE means no filter: the whole fleet is visible (CLAUDE.md §3).
    QVERIFY(context.vesselScope().isEmpty());
}

void TestInstallationRepository::loadReturnsStoredVesselRow()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                QStringLiteral("9074729"),
                                                &created));

    InstallationRepository reopened(db);

    std::optional<InstallationRecord> loaded;
    QVERIFY2(reopened.load(&loaded), qPrintable(reopened.errorString()));
    QVERIFY(loaded.has_value());

    // The name and IMO come back from the joined vessel row, which is what the
    // title bar needs (first-run-wizard-spec §9).
    QCOMPARE(loaded->mode, InstallationMode::Vessel);
    QCOMPARE(loaded->nodeId, QStringLiteral("VESSEL-9074729"));
    QCOMPARE(loaded->vesselName, QStringLiteral("MV Example"));
    QCOMPARE(loaded->vesselImoNumber, QStringLiteral("9074729"));

    const InstallationContext context(*loaded);
    QCOMPARE(context.vesselScope(), created.vesselId);
}

void TestInstallationRepository::auditColumnsArePopulated()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                QStringLiteral("9074729"),
                                                &created));

    for (const QString& table : {QStringLiteral("installation"), QStringLiteral("vessel")}) {
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("SELECT created_by, updated_by, origin_node,"
                                          " revision, is_deleted, created_at FROM %1")
                               .arg(table)));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("SYSTEM"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("SYSTEM"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("VESSEL-9074729"));
        QCOMPARE(query.value(3).toInt(), 1);
        QCOMPARE(query.value(4).toInt(), 0);
        // CLAUDE.md §6.2: an ISO-8601 UTC instant, so it ends in Z.
        QVERIFY(query.value(5).toString().endsWith(QLatin1Char('Z')));
    }
}

void TestInstallationRepository::rejectsInvalidImoNumber()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(!repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                 QStringLiteral("9074728"),
                                                 &created));
    QVERIFY(!repository.errorString().isEmpty());

    // Nothing was written on the way to discovering the number was wrong.
    QCOMPARE(rowCountOf(QStringLiteral("installation")), 0);
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 0);
}

void TestInstallationRepository::rejectsEmptyVesselName()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(!repository.createVesselInstallation(QStringLiteral("   "),
                                                 QStringLiteral("9074729"),
                                                 &created));
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 0);
}

void TestInstallationRepository::acceptsImoNumberWithPrefix()
{
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY2(repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                 QStringLiteral("IMO 9074729"),
                                                 &created),
             qPrintable(repository.errorString()));

    // Stored as digits only, whatever the user typed.
    QCOMPARE(created.vesselImoNumber, QStringLiteral("9074729"));
    QCOMPARE(created.nodeId, QStringLiteral("VESSEL-9074729"));
}

void TestInstallationRepository::rollsBackVesselWhenInstallationInsertFails()
{
    // first-run-wizard-spec §10.3. An existing installation row makes the
    // second installation insert fail on the singleton constraint, which is a
    // genuine failure arriving *after* the vessel insert has already succeeded
    // — exactly the half-written state the transaction exists to prevent.
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord office;
    QVERIFY(repository.createOfficeInstallation(&office));
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 0);

    InstallationRecord vessel;
    QVERIFY(!repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                 QStringLiteral("9074729"),
                                                 &vessel));
    QVERIFY(!repository.errorString().isEmpty());

    // The vessel row must have been rolled back with the failed installation.
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 0);
    QCOMPARE(rowCountOf(QStringLiteral("installation")), 1);
}

void TestInstallationRepository::imoNumberIsUniqueAtTheDatabaseLevel()
{
    // first-run-wizard-spec §10.5. The wizard only ever writes one vessel, but
    // step 4 and the sync engine will write many, so the constraint is checked
    // here where it is introduced.
    QSqlDatabase           db = database();
    InstallationRepository repository(db);

    InstallationRecord created;
    QVERIFY(repository.createVesselInstallation(QStringLiteral("MV Example"),
                                                QStringLiteral("9074729"),
                                                &created));

    QSqlQuery duplicate(db);
    duplicate.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by,"
        " updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES ('other-id', 'MV Impostor', '9074729', '2026-01-01T00:00:00Z', 'SYSTEM',"
        " '2026-01-01T00:00:00Z', 'SYSTEM', 0, 'OFFICE', 1)"));

    QVERIFY2(!duplicate.exec(), "a duplicate IMO number must be rejected");
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 1);
}

QTEST_MAIN(TestInstallationRepository)
#include "tst_InstallationRepository.moc"
