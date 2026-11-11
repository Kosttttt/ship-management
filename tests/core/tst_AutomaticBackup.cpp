#include "BackupTestSupport.h"

#include "core/AutomaticBackup.h"
#include "core/DatabaseBackup.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BackupTestSupport;

// database-backup-spec §4 and §7: when the daily backup is due, and the
// pruning rule — which is the one part of this feature that deletes
// anything, so what it must *not* touch is tested as carefully as what it
// does.
class TestAutomaticBackup : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void dueWhenTheFolderHasNothingForToday();
    void notDueWhenTodaysBackupIsAlreadyThere();
    void dueAgainWhenOnlyAnotherNodesBackupIsThere();
    void dueWhenTheFolderWasEmptied();

    void aMissingFolderIsReportedAndNotFatal();
    void noFolderChosenIsReportedAndNotFatal();

    void runningWhenDueWritesTodaysBackup();
    void runningWhenNotDueWritesNothing();

    void pruningKeepsTheTenNewest();
    void pruningLeavesEverythingElseAlone();
    void lastBackupTimeNamesTheNewest();

private:
    // Folders of their own per test: QTemporaryDir is a member, so without
    // this every test would inherit the previous one's files.
    QString folder() const { return QDir(m_work).filePath(QStringLiteral("backups")); }
    QString dbFolder() const { return QDir(m_work).filePath(QStringLiteral("db")); }
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // Writes a placeholder automatic backup for a node and date. The rules
    // under test read names, never contents, which is exactly why they can be
    // tested this way.
    bool placeAutomatic(const QString& nodeId, const QDate& date)
    {
        return touchFile(
            QDir(folder()).filePath(DatabaseBackup::automaticFileName(nodeId, date)));
    }

    static const QString& node()
    {
        static const QString value = QStringLiteral("VESSEL-9074729");
        return value;
    }

    QString       m_connectionName;
    QString       m_work;
    QTemporaryDir m_root;
};

void TestAutomaticBackup::init()
{
    QVERIFY(m_root.isValid());

    m_work = m_root.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(folder()));
    QVERIFY(QDir().mkpath(dbFolder()));

    m_connectionName = QStringLiteral("autobackup_%1").arg(QTest::currentTestFunction());
    QVERIFY(openFileDatabase(m_connectionName,
                             QDir(dbFolder()).filePath(QStringLiteral("source.db"))));

    QSqlDatabase db = database();
    QVERIFY(seedInstallation(db, QStringLiteral("VESSEL"), node(),
                             seedVessel(db, QStringLiteral("V UNO"),
                                        QStringLiteral("9074729"))));
}

void TestAutomaticBackup::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestAutomaticBackup::dueWhenTheFolderHasNothingForToday()
{
    QVERIFY(AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));

    // Yesterday's does not count for today.
    QVERIFY(placeAutomatic(node(), QDate(2026, 11, 10)));
    QVERIFY(AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));
}

void TestAutomaticBackup::notDueWhenTodaysBackupIsAlreadyThere()
{
    QVERIFY(placeAutomatic(node(), QDate(2026, 11, 11)));
    QVERIFY(!AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));
}

void TestAutomaticBackup::dueAgainWhenOnlyAnotherNodesBackupIsThere()
{
    // Two computers sharing one Google Drive folder (§2). The office's backup
    // says nothing about whether this vessel has made one.
    QVERIFY(placeAutomatic(QStringLiteral("OFFICE"), QDate(2026, 11, 11)));
    QVERIFY(AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));
}

void TestAutomaticBackup::dueWhenTheFolderWasEmptied()
{
    // §4: the decision comes from the files, not from a stored date, so a
    // folder that was emptied or swapped gets a fresh backup instead of
    // waiting until tomorrow.
    QVERIFY(placeAutomatic(node(), QDate(2026, 11, 11)));
    QVERIFY(!AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));

    QVERIFY(QFile::remove(
        QDir(folder()).filePath(DatabaseBackup::automaticFileName(node(), QDate(2026, 11, 11)))));
    QVERIFY(AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));
}

void TestAutomaticBackup::aMissingFolderIsReportedAndNotFatal()
{
    // The Google Drive folder that is not running, the USB drive that is not
    // plugged in. §4: log it, say so, carry on.
    const QString missing = QDir(folder()).filePath(QStringLiteral("not-mounted"));

    QSqlDatabase db = database();
    QString      saved;
    QString      error;
    QCOMPARE(AutomaticBackup::runIfDue(db, missing, node(), QDate(2026, 11, 11), &saved, &error),
             AutomaticBackup::Outcome::NoFolder);
    QVERIFY(saved.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("not there")), qPrintable(error));

    // And it is not created on the way past.
    QVERIFY(!QDir(missing).exists());
    QVERIFY(AutomaticBackup::existingDates(missing, node()).isEmpty());
}

void TestAutomaticBackup::noFolderChosenIsReportedAndNotFatal()
{
    QSqlDatabase db = database();
    QString      saved;
    QString      error;
    QCOMPARE(AutomaticBackup::runIfDue(db, QString(), node(), QDate(2026, 11, 11), &saved, &error),
             AutomaticBackup::Outcome::NoFolder);
    QVERIFY2(error.contains(QStringLiteral("No folder")), qPrintable(error));
}

void TestAutomaticBackup::runningWhenDueWritesTodaysBackup()
{
    QSqlDatabase db = database();
    QString      saved;
    QString      error;
    QCOMPARE(AutomaticBackup::runIfDue(db, folder(), node(), QDate(2026, 11, 11), &saved, &error),
             AutomaticBackup::Outcome::Saved);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(QFileInfo(saved).fileName(),
             DatabaseBackup::automaticFileName(node(), QDate(2026, 11, 11)));
    QCOMPARE(scalarFrom(saved, QStringLiteral("SELECT COUNT(*) FROM vessel")).toInt(), 1);

    // Having run, it is no longer due — which is the loop that stops this
    // happening on every start.
    QVERIFY(!AutomaticBackup::isDueToday(folder(), node(), QDate(2026, 11, 11)));
}

void TestAutomaticBackup::runningWhenNotDueWritesNothing()
{
    QVERIFY(placeAutomatic(node(), QDate(2026, 11, 11)));

    QSqlDatabase db = database();
    QString      saved;
    QString      error;
    QCOMPARE(AutomaticBackup::runIfDue(db, folder(), node(), QDate(2026, 11, 11), &saved, &error),
             AutomaticBackup::Outcome::AlreadyDoneToday);
    QVERIFY(saved.isEmpty());

    // The placeholder is still the placeholder: nothing overwrote it.
    QCOMPARE(QDir(folder()).entryList(QDir::Files).size(), 1);
}

void TestAutomaticBackup::pruningKeepsTheTenNewest()
{
    // Fourteen days of backups, oldest first.
    for (int day = 1; day <= 14; ++day) {
        QVERIFY(placeAutomatic(node(), QDate(2026, 11, day)));
    }

    QStringList removed;
    QCOMPARE(AutomaticBackup::prune(folder(), node(), AutomaticBackup::kKeepNewest, &removed), 4);

    const QList<QDate> left = AutomaticBackup::existingDates(folder(), node());
    QCOMPARE(left.size(), AutomaticBackup::kKeepNewest);

    // The newest are the ones kept, and the oldest the ones removed.
    QCOMPARE(left.first(), QDate(2026, 11, 14));
    QCOMPARE(left.last(), QDate(2026, 11, 5));
    QCOMPARE(removed.size(), 4);
}

void TestAutomaticBackup::pruningLeavesEverythingElseAlone()
{
    // Well past the limit for this node, so pruning definitely runs.
    for (int day = 1; day <= 12; ++day) {
        QVERIFY(placeAutomatic(node(), QDate(2026, 11, day)));
    }

    // §4: never a manual backup, never another computer's backups, never
    // anything else in the folder. These are all older than the files being
    // deleted, so a rule that sorted the whole folder by date would take them.
    const QStringList protectedFiles{
        DatabaseBackup::manualFileName(node(), QDateTime(QDate(2020, 1, 1), QTime(8, 0))),
        DatabaseBackup::automaticFileName(QStringLiteral("OFFICE"), QDate(2020, 1, 2)),
        QStringLiteral("ship-management.db"),
        QStringLiteral("Fleet list.xlsx"),
    };
    for (const QString& name : protectedFiles) {
        QVERIFY(touchFile(QDir(folder()).filePath(name)));
    }

    QCOMPARE(AutomaticBackup::prune(folder(), node(), AutomaticBackup::kKeepNewest), 2);

    for (const QString& name : protectedFiles) {
        QVERIFY2(QFile::exists(QDir(folder()).filePath(name)), qPrintable(name));
    }
}

void TestAutomaticBackup::lastBackupTimeNamesTheNewest()
{
    QVERIFY(AutomaticBackup::lastBackupTime(folder(), node()).isNull());

    QVERIFY(placeAutomatic(node(), QDate(2026, 11, 10)));
    QVERIFY(placeAutomatic(node(), QDate(2026, 11, 11)));

    // The file's own timestamp, so the Settings line can show a time and not
    // just a day (§3). Both were written just now, so all that can be
    // asserted is that there is one.
    QVERIFY(AutomaticBackup::lastBackupTime(folder(), node()).isValid());

    // Another node's backup is not this node's "last automatic backup".
    QVERIFY(AutomaticBackup::lastBackupTime(folder(), QStringLiteral("OFFICE")).isNull());
}

QTEST_MAIN(TestAutomaticBackup)
#include "tst_AutomaticBackup.moc"
