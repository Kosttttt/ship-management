#include "BackupTestSupport.h"

#include "core/RestoreSwap.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BackupTestSupport;

// database-backup-spec §5.3 and §7: staging a checked backup, and the swap
// that happens at the next start before the database is opened.
//
// Everything runs inside a QTemporaryDir standing in for the data folder.
// Nothing here can reach the real one, which is the whole point of the folder
// being a parameter (§6, CLAUDE.md §10.1).
class TestRestoreSwap : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void stagingPutsThePendingFileInTheDataFolder();
    void stagingLeavesNoTemporaryFileBehind();
    void stagingReplacesAnEarlierPendingRestore();

    void withNoPendingFileNothingChanges();
    void thePendingFileBecomesTheDatabase();
    void theOldDatabaseIsKeptAsASafetyCopy();
    void sideFilesOfTheOldDatabaseTravelWithIt();
    void aSideFileThatWillNotMoveCancelsTheRestore();
    void withNoDatabaseYetThePendingFileSimplyBecomesIt();
    void aFailedSwapPutsTheOriginalBack();

private:
    // Folders of their own per test: QTemporaryDir is a member, so without
    // this every test would inherit the previous one's files.
    QString dataFolder() const { return QDir(m_work).filePath(QStringLiteral("data")); }
    QString elsewhere() const { return QDir(m_work).filePath(QStringLiteral("elsewhere")); }
    QString livePath() const { return RestoreSwap::liveFilePath(dataFolder()); }
    QString pendingPath() const { return RestoreSwap::pendingFilePath(dataFolder()); }
    QString backupPath() const { return QDir(elsewhere()).filePath(QStringLiteral("backup.db")); }

    QStringList safetyCopies() const
    {
        return QDir(dataFolder())
            .entryList({QStringLiteral("ship-management.before-restore-*.db")}, QDir::Files);
    }

    QString       m_work;
    QTemporaryDir m_root;
};

void TestRestoreSwap::init()
{
    QVERIFY(m_root.isValid());

    m_work = m_root.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(dataFolder()));
    QVERIFY(QDir().mkpath(elsewhere()));
}

void TestRestoreSwap::stagingPutsThePendingFileInTheDataFolder()
{
    QVERIFY(makeDatabaseFile(backupPath(), QStringLiteral("OFFICE"), /*certificateCount=*/3));

    QString error;
    QVERIFY2(RestoreSwap::stage(dataFolder(), backupPath(), &error), qPrintable(error));

    QVERIFY(QFile::exists(pendingPath()));
    QCOMPARE(scalarFrom(pendingPath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(),
             3);
}

void TestRestoreSwap::stagingLeavesNoTemporaryFileBehind()
{
    QVERIFY(makeDatabaseFile(backupPath(), QStringLiteral("OFFICE")));

    QString error;
    QVERIFY2(RestoreSwap::stage(dataFolder(), backupPath(), &error), qPrintable(error));

    // A short .tmp left in the data folder would be harmless, but a *renamed*
    // short file would be read as a database at the next start — which is why
    // the copy is finished before it is named (§5.3 step 1).
    QVERIFY(!QFile::exists(pendingPath() + QStringLiteral(".tmp")));
}

void TestRestoreSwap::stagingReplacesAnEarlierPendingRestore()
{
    // Somebody chose a backup, then changed their mind and chose another
    // before restarting.
    QVERIFY(touchFile(pendingPath()));
    QVERIFY(makeDatabaseFile(backupPath(), QStringLiteral("OFFICE"), /*certificateCount=*/2));

    QString error;
    QVERIFY2(RestoreSwap::stage(dataFolder(), backupPath(), &error), qPrintable(error));
    QCOMPARE(scalarFrom(pendingPath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(),
             2);
}

void TestRestoreSwap::withNoPendingFileNothingChanges()
{
    QVERIFY(makeDatabaseFile(livePath(), QStringLiteral("OFFICE")));
    const QStringList before = QDir(dataFolder()).entryList(QDir::Files);

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail),
             RestoreSwap::Outcome::NothingPending);

    // The ordinary start: every start that is not a restore goes through this
    // call, so it must be exactly nothing.
    QCOMPARE(QDir(dataFolder()).entryList(QDir::Files), before);
    QVERIFY(detail.isEmpty());
}

void TestRestoreSwap::thePendingFileBecomesTheDatabase()
{
    QVERIFY(makeDatabaseFile(livePath(), QStringLiteral("OFFICE"), /*certificateCount=*/1));
    QVERIFY(makeDatabaseFile(pendingPath(), QStringLiteral("OFFICE"), /*certificateCount=*/7));

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail), RestoreSwap::Outcome::Restored);

    // The database the program is about to open is the backup's.
    QCOMPARE(scalarFrom(livePath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(), 7);
    QVERIFY(!QFile::exists(pendingPath()));
}

void TestRestoreSwap::theOldDatabaseIsKeptAsASafetyCopy()
{
    QVERIFY(makeDatabaseFile(livePath(), QStringLiteral("OFFICE"), /*certificateCount=*/5));
    QVERIFY(makeDatabaseFile(pendingPath(), QStringLiteral("OFFICE"), /*certificateCount=*/2));

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail), RestoreSwap::Outcome::Restored);

    // §1: nothing is ever deleted by a restore. The replaced database is
    // beside the new one, with its own contents intact.
    const QStringList copies = safetyCopies();
    QCOMPARE(copies.size(), 1);
    QCOMPARE(scalarFrom(QDir(dataFolder()).filePath(copies.first()),
                        QStringLiteral("SELECT COUNT(*) FROM certificate"))
                 .toInt(),
             5);
    QVERIFY2(detail.contains(QStringLiteral("kept")), qPrintable(detail));
}

void TestRestoreSwap::sideFilesOfTheOldDatabaseTravelWithIt()
{
    QVERIFY(makeDatabaseFile(livePath(), QStringLiteral("OFFICE")));
    QVERIFY(makeDatabaseFile(pendingPath(), QStringLiteral("OFFICE")));

    // A -wal left beside the restored database would be applied to it, and it
    // belongs to a different database: that is how a good file gets ruined.
    QVERIFY(touchFile(livePath() + QStringLiteral("-wal")));
    QVERIFY(touchFile(livePath() + QStringLiteral("-shm")));

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail), RestoreSwap::Outcome::Restored);

    QVERIFY(!QFile::exists(livePath() + QStringLiteral("-wal")));
    QVERIFY(!QFile::exists(livePath() + QStringLiteral("-shm")));

    const QStringList copies = safetyCopies();
    QCOMPARE(copies.size(), 1);
    const QString safety = QDir(dataFolder()).filePath(copies.first());
    QVERIFY(QFile::exists(safety + QStringLiteral("-wal")));
    QVERIFY(QFile::exists(safety + QStringLiteral("-shm")));
}

void TestRestoreSwap::withNoDatabaseYetThePendingFileSimplyBecomesIt()
{
    // A fresh installation told to restore before it has ever opened a
    // database: there is nothing to keep, and no safety copy to make.
    QVERIFY(makeDatabaseFile(pendingPath(), QStringLiteral("OFFICE"), /*certificateCount=*/3));

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail), RestoreSwap::Outcome::Restored);

    QCOMPARE(scalarFrom(livePath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(), 3);
    QVERIFY(safetyCopies().isEmpty());
}


void TestRestoreSwap::aSideFileThatWillNotMoveCancelsTheRestore()
{
#ifndef Q_OS_WIN
    QSKIP("Forcing a rename to fail needs Windows file-sharing rules; on POSIX an open "
          "file renames happily. The recovery path is the same code on every platform.");
#endif

    // §10.1. A -journal beside the database means the last session did not
    // end cleanly, and that journal holds what is needed to repair the
    // database — so it must not be thrown away to let the restore through.
    // An earlier version removed it and carried on, which could leave the
    // safety copy damaged.
    QVERIFY(makeDatabaseFile(livePath(), QStringLiteral("OFFICE"), /*certificateCount=*/4));
    QVERIFY(makeDatabaseFile(pendingPath(), QStringLiteral("OFFICE"), /*certificateCount=*/9));

    const QString journal = livePath() + QStringLiteral("-journal");
    QVERIFY(touchFile(journal));
    const QByteArray journalBefore = contentsOf(journal);

    // Held open, which is how this actually goes wrong: something else has
    // the file. On Windows Qt opens without FILE_SHARE_DELETE, so the rename
    // fails.
    QFile holder(journal);
    QVERIFY(holder.open(QIODevice::ReadOnly));

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail), RestoreSwap::Outcome::Failed);
    holder.close();

    // Every file assertion comes before anything opens a database, and that
    // order is load-bearing: SQLite rolls back and then *deletes* a journal
    // it finds beside a database it opens, so a read here would destroy the
    // very thing being checked for.
    QVERIFY2(QFile::exists(livePath()), "the folder must never be left without a database");
    QVERIFY2(QFile::exists(journal), "the journal must be put back, not deleted");
    QCOMPARE(contentsOf(journal), journalBefore);

    // Nothing half-moved, and the restore still waiting to be tried again
    // once whatever holds the file lets go.
    QVERIFY(safetyCopies().isEmpty());
    QVERIFY(QFile::exists(pendingPath()));

    QVERIFY2(detail.contains(QStringLiteral("is in use")), qPrintable(detail));
    QVERIFY2(detail.contains(QStringLiteral("untouched")), qPrintable(detail));

    // Last, because these open both files: the database is the one that was
    // there, and the pending file is still the backup that was chosen.
    QCOMPARE(scalarFrom(livePath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(), 4);
    QCOMPARE(scalarFrom(pendingPath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(),
             9);
}
void TestRestoreSwap::aFailedSwapPutsTheOriginalBack()
{
#ifndef Q_OS_WIN
    QSKIP("Forcing a rename to fail needs Windows file-sharing rules; on POSIX an open "
          "file renames happily. The recovery path is the same code on every platform.");
#endif

    QVERIFY(makeDatabaseFile(livePath(), QStringLiteral("OFFICE"), /*certificateCount=*/6));
    QVERIFY(makeDatabaseFile(pendingPath(), QStringLiteral("OFFICE"), /*certificateCount=*/2));

    // Holding the pending file open is how this actually goes wrong in
    // practice — a sync client or a virus scanner with the file open — and on
    // Windows it makes the rename fail, because Qt opens without
    // FILE_SHARE_DELETE. So the first rename succeeds, the second does not,
    // and §5.3's recovery has to put the original database back.
    QFile holder(pendingPath());
    QVERIFY(holder.open(QIODevice::ReadOnly));

    QString detail;
    QCOMPARE(RestoreSwap::applyPending(dataFolder(), &detail), RestoreSwap::Outcome::Failed);
    holder.close();

    QVERIFY2(QFile::exists(livePath()), "the folder must never be left without a database");
    QCOMPARE(scalarFrom(livePath(), QStringLiteral("SELECT COUNT(*) FROM certificate")).toInt(), 6);
    QVERIFY(safetyCopies().isEmpty());
    QVERIFY2(detail.contains(QStringLiteral("untouched")), qPrintable(detail));
}

QTEST_MAIN(TestRestoreSwap)
#include "tst_RestoreSwap.moc"
