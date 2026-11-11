#include "BackupTestSupport.h"

#include "core/DatabaseBackup.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BackupTestSupport;

// database-backup-spec §2 and §7: VACUUM INTO writes a complete copy, the
// temporary name never survives, and a backup that fails its own check is
// not left behind looking good.
class TestDatabaseBackup : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theBackupHasTheSameRowsAsTheOriginal();
    void theTemporaryFileIsGoneAfterwards();
    void aMissingFolderIsReportedRatherThanCreated();
    void anExistingBackupIsReplacedOnlyAfterTheNewOneIsComplete();
    void aLeftoverTemporaryFileDoesNotBlockTheNextBackup();

    void manualAndAutomaticNamesFollowTheSpec();
    void onlyThisNodesAutomaticNamesAreRecognised();

private:
    // A folder of its own per test: QTemporaryDir is a member, so without
    // this every test would inherit the previous one's files.
    QString sourcePath() const { return QDir(m_work).filePath(QStringLiteral("source.db")); }
    QString outFolder() const { return QDir(m_work).filePath(QStringLiteral("out")); }
    QString targetPath() const { return QDir(outFolder()).filePath(QStringLiteral("backup.db")); }
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString       m_connectionName;
    QString       m_work;
    QTemporaryDir m_folder;
};

void TestDatabaseBackup::init()
{
    QVERIFY(m_folder.isValid());

    m_work = m_folder.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(outFolder()));

    m_connectionName = QStringLiteral("dbbackup_%1").arg(QTest::currentTestFunction());
    QVERIFY(openFileDatabase(m_connectionName, sourcePath()));

    QSqlDatabase db = database();
    QVERIFY(seedInstallation(db, QStringLiteral("OFFICE"), QStringLiteral("OFFICE")));
    const QString vesselId = seedVessel(db, QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    QVERIFY(!vesselId.isEmpty());
    QVERIFY(seedCertificate(db, vesselId, QStringLiteral("Safety Construction")));
    QVERIFY(seedCertificate(db, vesselId, QStringLiteral("Load Line")));
}

void TestDatabaseBackup::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDatabaseBackup::theBackupHasTheSameRowsAsTheOriginal()
{
    QSqlDatabase db = database();
    QString      error;
    QVERIFY2(DatabaseBackup::writeTo(db, targetPath(), &error), qPrintable(error));
    QVERIFY(QFile::exists(targetPath()));

    // Opened as a database of its own, not compared byte for byte: VACUUM
    // rewrites the file, so the bytes legitimately differ while the data must
    // not.
    QCOMPARE(scalarFrom(targetPath(), QStringLiteral("SELECT COUNT(*) FROM vessel")).toInt(), 1);
    QCOMPARE(scalarFrom(targetPath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(),
             2);
    QCOMPARE(scalarFrom(targetPath(), QStringLiteral("SELECT node_id FROM installation")).toString(),
             QStringLiteral("OFFICE"));

    // The schema came with it, so the restored copy needs no special handling.
    QCOMPARE(scalarFrom(targetPath(),
                        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table'"
                                       " AND name = 'certificate_attachment'"))
                 .toInt(),
             1);
}

void TestDatabaseBackup::theTemporaryFileIsGoneAfterwards()
{
    QSqlDatabase db = database();
    QString      error;
    QVERIFY2(DatabaseBackup::writeTo(db, targetPath(), &error), qPrintable(error));

    // §2: a sync client must never be left with a half-written file, nor with
    // a stray .tmp sitting in the backup folder.
    QVERIFY(!QFile::exists(targetPath() + QStringLiteral(".tmp")));

    const QStringList left = QDir(outFolder()).entryList(QDir::Files);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left.first(), QStringLiteral("backup.db"));
}

void TestDatabaseBackup::aMissingFolderIsReportedRatherThanCreated()
{
    // The unplugged USB drive, the Google Drive folder that is not running.
    // §4 needs this detectable so startup can carry on and say so.
    const QString missing =
        QDir(outFolder()).filePath(QStringLiteral("not-there/backup.db"));

    QSqlDatabase db = database();
    QString      error;
    QVERIFY(!DatabaseBackup::writeTo(db, missing, &error));
    QVERIFY2(error.contains(QStringLiteral("not there")), qPrintable(error));
    QVERIFY(!QDir(outFolder()).exists(QStringLiteral("not-there")));

    // A failed backup leaves nothing at all behind, which is the half of
    // §2's "never left looking like a good backup" that a test can reach:
    // making VACUUM INTO produce a genuinely damaged file is not something
    // this suite can arrange. That the check itself rejects a damaged file is
    // in tst_BackupFileInspector.
    QVERIFY(QDir(outFolder()).entryList(QDir::Files).isEmpty());
}

void TestDatabaseBackup::anExistingBackupIsReplacedOnlyAfterTheNewOneIsComplete()
{
    // Standing in for a file the user chose in the save dialog and confirmed
    // overwriting. It is not a database, so if it were still there afterwards
    // the check below would read nothing.
    QVERIFY(touchFile(targetPath()));

    QSqlDatabase db = database();
    QString      error;
    QVERIFY2(DatabaseBackup::writeTo(db, targetPath(), &error), qPrintable(error));

    QCOMPARE(scalarFrom(targetPath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(),
             2);
    QVERIFY(!QFile::exists(targetPath() + QStringLiteral(".tmp")));
}

void TestDatabaseBackup::aLeftoverTemporaryFileDoesNotBlockTheNextBackup()
{
    // VACUUM INTO refuses an existing target, so an interrupted run would
    // otherwise poison every later attempt.
    QVERIFY(touchFile(targetPath() + QStringLiteral(".tmp")));

    QSqlDatabase db = database();
    QString      error;
    QVERIFY2(DatabaseBackup::writeTo(db, targetPath(), &error), qPrintable(error));
    QCOMPARE(scalarFrom(targetPath(), QStringLiteral("SELECT COUNT(*) FROM vessel")).toInt(), 1);
}

void TestDatabaseBackup::manualAndAutomaticNamesFollowTheSpec()
{
    const QDateTime when(QDate(2026, 11, 11), QTime(9, 12));

    QCOMPARE(DatabaseBackup::manualFileName(QStringLiteral("OFFICE"), when),
             QStringLiteral("ship-management-OFFICE-2026-11-11_0912.db"));
    QCOMPARE(DatabaseBackup::automaticFileName(QStringLiteral("VESSEL-9074729"), when.date()),
             QStringLiteral("ship-management-auto-VESSEL-9074729-2026-11-11.db"));
}

void TestDatabaseBackup::onlyThisNodesAutomaticNamesAreRecognised()
{
    const QString node = QStringLiteral("VESSEL-9074729");

    QVERIFY(DatabaseBackup::isAutomaticFileName(
        QStringLiteral("ship-management-auto-VESSEL-9074729-2026-11-11.db"), node));
    QCOMPARE(DatabaseBackup::dateOfAutomaticFileName(
                 QStringLiteral("ship-management-auto-VESSEL-9074729-2026-11-11.db"), node),
             QDate(2026, 11, 11));

    // Everything pruning must never touch (§4).
    QVERIFY(!DatabaseBackup::isAutomaticFileName(
        QStringLiteral("ship-management-VESSEL-9074729-2026-11-11_0912.db"), node)); // manual
    QVERIFY(!DatabaseBackup::isAutomaticFileName(
        QStringLiteral("ship-management-auto-OFFICE-2026-11-11.db"), node)); // another node
    QVERIFY(!DatabaseBackup::isAutomaticFileName(QStringLiteral("holiday-photos.db"), node));
    QVERIFY(!DatabaseBackup::isAutomaticFileName(
        QStringLiteral("ship-management-auto-VESSEL-9074729-2026-11-11.db.tmp"), node));

    // A node id that merely begins with this one. The date that follows is
    // what rules it out, which is why the check parses rather than trusts the
    // prefix.
    QVERIFY(!DatabaseBackup::isAutomaticFileName(
        QStringLiteral("ship-management-auto-VESSEL-90747291-2026-11-11.db"), node));
}

QTEST_MAIN(TestDatabaseBackup)
#include "tst_DatabaseBackup.moc"
