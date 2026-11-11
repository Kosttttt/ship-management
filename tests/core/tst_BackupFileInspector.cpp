#include "BackupTestSupport.h"

#include "core/BackupFileInspector.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BackupTestSupport;

// database-backup-spec §5.1 and §7: what a file has to be before a restore is
// even offered, and the numbers §5.2's confirmation table shows.
class TestBackupFileInspector : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void aCompleteBackupIsAccepted();
    void theSummaryReportsWhatIsInTheFile();
    void aVesselInstallationIsNamedInTheSummary();

    void aFileThatIsNotADatabaseIsRefused();
    void aMissingFileIsRefused();
    void aDatabaseWithoutOurTablesIsRefused();
    void aDatabaseWithNoInstallationRowIsRefused();
    void aDatabaseWithTwoInstallationRowsIsRefused();
    void aNewerSchemaIsRefused();
    void anOlderSchemaIsAccepted();

private:
    // A folder of its own per test: QTemporaryDir is a member, so without
    // this every test would inherit the previous one's files.
    QString pathFor(const QString& name) const { return QDir(m_work).filePath(name); }

    // What this program carries. Every check but the version one is
    // independent of it, so a fixed, generous number keeps those tests from
    // needing a new migration every round.
    static constexpr int kKnownMigrations = 11;

    QString       m_work;
    QTemporaryDir m_folder;
};

void TestBackupFileInspector::init()
{
    QVERIFY(m_folder.isValid());

    m_work = m_folder.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(m_work));
}

void TestBackupFileInspector::aCompleteBackupIsAccepted()
{
    const QString path = pathFor(QStringLiteral("good.db"));
    QVERIFY(makeDatabaseFile(path, QStringLiteral("OFFICE")));

    BackupSummary summary;
    QString       error;
    QVERIFY2(BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error),
             qPrintable(error));
    QVERIFY(error.isEmpty());
}

void TestBackupFileInspector::theSummaryReportsWhatIsInTheFile()
{
    const QString path = pathFor(QStringLiteral("counts.db"));
    QVERIFY(makeDatabaseFile(path, QStringLiteral("OFFICE"), /*certificateCount=*/4));

    BackupSummary summary;
    QString       error;
    QVERIFY2(BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error),
             qPrintable(error));

    QCOMPARE(summary.nodeId, QStringLiteral("OFFICE"));
    QCOMPARE(summary.modeLabel(), QStringLiteral("OFFICE"));
    QCOMPARE(summary.vesselCount, 1);
    QCOMPARE(summary.certificateCount, 4);
    QVERIFY(summary.schemaVersion > 0);

    // The §5.2 "Last change" line, read from the seeded updated_at values.
    QVERIFY(summary.lastChangeUtc.isValid());
    QCOMPARE(summary.lastChangeUtc.date(), QDate(2026, 1, 1));
}

void TestBackupFileInspector::aVesselInstallationIsNamedInTheSummary()
{
    // §5.2: when the backup's mode or node differs from this computer's, the
    // dialog says which vessel it is about — so the name has to come back.
    const QString connectionName = QStringLiteral("inspect_vessel");
    const QString path            = pathFor(QStringLiteral("vessel.db"));
    QVERIFY(openFileDatabase(connectionName, path));

    bool seeded = false;
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        const QString vesselId =
            seedVessel(db, QStringLiteral("V UNO"), QStringLiteral("9074729"));
        seeded = !vesselId.isEmpty()
                 && seedInstallation(db, QStringLiteral("VESSEL"),
                                     QStringLiteral("VESSEL-9074729"), vesselId);
    }
    closeDatabase(connectionName);
    QVERIFY(seeded);

    BackupSummary summary;
    QString       error;
    QVERIFY2(BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error),
             qPrintable(error));

    QVERIFY(summary.isVesselMode);
    QCOMPARE(summary.modeLabel(), QStringLiteral("VESSEL"));
    QCOMPARE(summary.nodeId, QStringLiteral("VESSEL-9074729"));
    QCOMPARE(summary.vesselName, QStringLiteral("V UNO"));
}

void TestBackupFileInspector::aFileThatIsNotADatabaseIsRefused()
{
    const QString path = pathFor(QStringLiteral("notes.txt"));
    QVERIFY(touchFile(path));

    BackupSummary summary;
    QString       error;
    QVERIFY(!BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error));
    QVERIFY(!error.isEmpty());

    // Also the half of §2 that tst_DatabaseBackup cannot reach: a file that
    // is not a sound database fails the check a fresh backup is put through.
    QString integrityError;
    QVERIFY(!BackupFileInspector::passesIntegrityCheck(path, &integrityError));
    QVERIFY(!integrityError.isEmpty());
}

void TestBackupFileInspector::aMissingFileIsRefused()
{
    BackupSummary summary;
    QString       error;
    QVERIFY(!BackupFileInspector::inspect(pathFor(QStringLiteral("absent.db")), kKnownMigrations,
                                          &summary, &error));
    QVERIFY2(error.contains(QStringLiteral("no file")), qPrintable(error));
}

void TestBackupFileInspector::aDatabaseWithoutOurTablesIsRefused()
{
    // A real, sound SQLite database — just somebody else's.
    const QString path           = pathFor(QStringLiteral("stranger.db"));
    const QString connectionName = QStringLiteral("inspect_stranger");

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(path);
    QVERIFY(db.open());
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE recipes (id INTEGER, name TEXT)")));
    closeDatabase(connectionName);

    BackupSummary summary;
    QString       error;
    QVERIFY(!BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error));
    QVERIFY2(error.contains(QStringLiteral("not one belonging to this program")),
             qPrintable(error));
}

void TestBackupFileInspector::aDatabaseWithNoInstallationRowIsRefused()
{
    // The schema without the identity: a database that was created but never
    // set up, which says nothing about which computer it belongs to.
    const QString connectionName = QStringLiteral("inspect_noinstall");
    const QString path            = pathFor(QStringLiteral("blank.db"));
    QVERIFY(openFileDatabase(connectionName, path));
    closeDatabase(connectionName);

    BackupSummary summary;
    QString       error;
    QVERIFY(!BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error));
    QVERIFY2(error.contains(QStringLiteral("no installation row")), qPrintable(error));
}

void TestBackupFileInspector::aDatabaseWithTwoInstallationRowsIsRefused()
{
    const QString connectionName = QStringLiteral("inspect_twoinstall");
    const QString path            = pathFor(QStringLiteral("two.db"));
    QVERIFY(openFileDatabase(connectionName, path));

    bool seeded = false;
    {
        // migrations/001 makes a second row impossible twice over: `singleton`
        // is UNIQUE and CHECK-ed to equal 1. The second row therefore needs a
        // different singleton value *and* the CHECK suspended, which is the
        // only way to produce the shape a hand-edited file could arrive in —
        // and the reason the inspector checks for it rather than trusting the
        // constraint.
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        QSqlQuery    pragma(db);
        seeded = pragma.exec(QStringLiteral("PRAGMA ignore_check_constraints = ON"))
                 && seedInstallation(db, QStringLiteral("OFFICE"), QStringLiteral("OFFICE"),
                                     QString(), QStringLiteral("install-1"), /*singleton=*/true)
                 && seedInstallation(db, QStringLiteral("OFFICE"), QStringLiteral("OFFICE-2"),
                                     QString(), QStringLiteral("install-2"), /*singleton=*/false);
    }
    closeDatabase(connectionName);
    QVERIFY(seeded);

    BackupSummary summary;
    QString       error;
    QVERIFY(!BackupFileInspector::inspect(path, kKnownMigrations, &summary, &error));
    QVERIFY2(error.contains(QStringLiteral("more than one installation row")), qPrintable(error));
}

void TestBackupFileInspector::aNewerSchemaIsRefused()
{
    const QString path = pathFor(QStringLiteral("newer.db"));
    QVERIFY(makeDatabaseFile(path, QStringLiteral("OFFICE")));

    // Standing in for a backup from a later version of the program: this one
    // knows fewer migrations than the file has had applied, and has no idea
    // what the ones it is missing did to the data.
    BackupSummary summary;
    QString       error;
    QVERIFY(!BackupFileInspector::inspect(path, /*highestKnownMigration=*/1, &summary, &error));
    QVERIFY2(error.contains(QStringLiteral("newer version of the program")), qPrintable(error));
}

void TestBackupFileInspector::anOlderSchemaIsAccepted()
{
    const QString path = pathFor(QStringLiteral("older.db"));
    QVERIFY(makeDatabaseFile(path, QStringLiteral("OFFICE")));

    // §5.1: an older backup is fine — the usual migrations bring it up to
    // date on the next start. Asserted against a number far above anything
    // this project will reach soon, so the rule is tested rather than today's
    // migration count.
    BackupSummary summary;
    QString       error;
    QVERIFY2(BackupFileInspector::inspect(path, /*highestKnownMigration=*/9999, &summary, &error),
             qPrintable(error));
    QVERIFY(summary.schemaVersion < 9999);
}

QTEST_MAIN(TestBackupFileInspector)
#include "tst_BackupFileInspector.moc"
