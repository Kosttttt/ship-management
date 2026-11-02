#include "VesselTestSupport.h"

#include "core/ManagedFileStore.h"
#include "core/VesselAttachmentRoots.h"
#include "core/VesselRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace VesselTestSupport;

// certificate-attachment-addendum-spec §2, and §7 cases 1 to 4.
class TestVesselAttachmentRoots : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §2.2 — the refusal, and the absence of any fallback.
    void aVesselWithNoFolderRefusesAndSaysWhereToSetOne();    // case 1
    void anUnknownVesselRefusesRatherThanGuessing();
    void aConfiguredVesselResolvesToItsOwnFolder();
    void theTestOverrideStillWinsOverTheVesselsOwnFolder();   // §2.4

    // §2.5 — changing the folder.
    void settingAFolderForTheFirstTimeMovesNothing();         // case 2
    void changingTheFolderMovesTheDocumentsAndKeepsThePath(); // case 3
    void aFolderAlreadyHoldingAttachmentsIsRefused();         // case 4
    void aFolderThatDoesNotExistIsRefused();
    void choosingTheSameFolderAgainIsAcceptedAndMovesNothing();

    // §2.2 — opening is never blocked.
    void anAttachmentStoredBeforeTheAddendumStillResolves();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static bool writeFile(const QString& absolutePath, const QString& contents)
    {
        if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
            return false;
        }
        QFile file(absolutePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        return file.write(contents.toUtf8()) == contents.toUtf8().size();
    }

    static QString readFile(const QString& absolutePath)
    {
        QFile file(absolutePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }

    // The relative path a certificate would have stored, under whichever root
    // is in force.
    static QString storedRelativePath()
    {
        return QStringLiteral("attachments/c1/15D_Load_Line_2031-02-04.pdf");
    }

    static QString otherVesselsPath()
    {
        return QStringLiteral("attachments/other/theirs.pdf");
    }

    QString m_connectionName;
};

void TestVesselAttachmentRoots::init()
{
    m_connectionName = QStringLiteral("roots_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    // Cleared for this whole suite, deliberately: the override exists so the
    // certificate suites need not configure a vessel at all, and leaving it set
    // here would short-circuit the very lookup under test (§2.4).
    ManagedFileStore::setRootPathOverride(QString());
}

void TestVesselAttachmentRoots::cleanup()
{
    ManagedFileStore::setRootPathOverride(QString());
    closeDatabase(m_connectionName);
}

void TestVesselAttachmentRoots::aVesselWithNoFolderRefusesAndSaysWhereToSetOne()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));
    QVERIFY(!vesselId.isEmpty());

    QString       error;
    const QString root = roots.rootForWriting(vesselId, &error);

    QVERIFY2(root.isEmpty(), "an unconfigured vessel must not resolve to anywhere at all");

    // The point of §2.2 is that this is a refusal rather than a fallback, and
    // that the message says what to do about it.
    QVERIFY2(error.contains(QStringLiteral("Settings")), qPrintable(error));
}

void TestVesselAttachmentRoots::anUnknownVesselRefusesRatherThanGuessing()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    QString error;
    QVERIFY(roots.rootForWriting(QStringLiteral("no-such-vessel"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    QVERIFY(roots.rootForWriting(QString(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestVesselAttachmentRoots::aConfiguredVesselResolvesToItsOwnFolder()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));
    QTemporaryDir folder;
    QVERIFY(folder.isValid());

    QString error;
    QVERIFY2(roots.changeRoot(vesselId, folder.path(), &error), qPrintable(error));

    QCOMPARE(roots.rootForWriting(vesselId, &error),
             QFileInfo(folder.path()).absoluteFilePath());
    QVERIFY(error.isEmpty());
}

void TestVesselAttachmentRoots::theTestOverrideStillWinsOverTheVesselsOwnFolder()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));
    QTemporaryDir vesselFolder;
    QTemporaryDir overridden;
    QVERIFY(vesselFolder.isValid());
    QVERIFY(overridden.isValid());

    QString error;
    QVERIFY(roots.changeRoot(vesselId, vesselFolder.path(), &error));

    ManagedFileStore::setRootPathOverride(overridden.path());

    // §2.4: checked first, before any vessel lookup. This is what lets the
    // certificate suites attach to vessels that have no row at all.
    QCOMPARE(roots.rootForWriting(vesselId, &error),
             QFileInfo(overridden.path()).absoluteFilePath());
}

void TestVesselAttachmentRoots::settingAFolderForTheFirstTimeMovesNothing()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));
    QTemporaryDir folder;
    QVERIFY(folder.isValid());

    QString error;
    QVERIFY2(roots.changeRoot(vesselId, folder.path(), &error), qPrintable(error));

    // §7 case 2: nothing existed from this vessel's point of view, so nothing
    // was created either — the folder is left exactly as the user chose it.
    QVERIFY2(!QFileInfo::exists(QDir(folder.path()).filePath(
                 VesselAttachmentRoots::kManagedSubfolder)),
             "configuring a folder must not create anything inside it on its own");
}

void TestVesselAttachmentRoots::changingTheFolderMovesTheDocumentsAndKeepsThePath()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));

    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    QString error;
    QVERIFY(roots.changeRoot(vesselId, first.path(), &error));

    // A document where a real attachment would be.
    const QString relative = storedRelativePath();
    QVERIFY(writeFile(QDir(first.path()).filePath(relative), QStringLiteral("the scan")));

    QVERIFY2(roots.changeRoot(vesselId, second.path(), &error), qPrintable(error));

    // §7 case 3: the file physically relocated...
    QCOMPARE(readFile(QDir(second.path()).filePath(relative)), QStringLiteral("the scan"));
    QVERIFY2(!QFileInfo::exists(QDir(first.path()).filePath(relative)),
             "a move leaves nothing behind, or the two copies drift apart");

    // ...and the stored relative path did not have to change, because only the
    // root changed. That is CLAUDE.md §6.9 doing its job.
    QCOMPARE(roots.resolveForOpening(vesselId, relative),
             QDir(QFileInfo(second.path()).absoluteFilePath()).filePath(relative));
}

void TestVesselAttachmentRoots::aFolderAlreadyHoldingAttachmentsIsRefused()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));

    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    QString error;
    QVERIFY(roots.changeRoot(vesselId, first.path(), &error));
    QVERIFY(writeFile(QDir(first.path()).filePath(storedRelativePath()),
                      QStringLiteral("ours")));

    // Another vessel's folder, recognisable by the one thing §2.5 looks for.
    QVERIFY(writeFile(QDir(second.path()).filePath(otherVesselsPath()),
                      QStringLiteral("theirs")));

    QVERIFY2(!roots.changeRoot(vesselId, second.path(), &error),
             "a folder already holding attachments must be refused, not merged into");
    QVERIFY(!error.isEmpty());

    // §7 case 4: nothing moved and nothing changed, on either side.
    QCOMPARE(readFile(QDir(first.path()).filePath(storedRelativePath())),
             QStringLiteral("ours"));
    QCOMPARE(readFile(QDir(second.path()).filePath(otherVesselsPath())),
             QStringLiteral("theirs"));
    QCOMPARE(roots.rootForWriting(vesselId, &error),
             QFileInfo(first.path()).absoluteFilePath());
}

void TestVesselAttachmentRoots::aFolderThatDoesNotExistIsRefused()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));

    QTemporaryDir parent;
    QVERIFY(parent.isValid());

    QString error;
    QVERIFY(!roots.changeRoot(vesselId,
                              QDir(parent.path()).filePath(QStringLiteral("not-there")),
                              &error));
    QVERIFY(!error.isEmpty());

    // Still unconfigured: a refused folder is not half stored.
    QVERIFY(roots.rootForWriting(vesselId, &error).isEmpty());
}

void TestVesselAttachmentRoots::choosingTheSameFolderAgainIsAcceptedAndMovesNothing()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));
    QTemporaryDir folder;
    QVERIFY(folder.isValid());

    QString error;
    QVERIFY(roots.changeRoot(vesselId, folder.path(), &error));
    QVERIFY(writeFile(QDir(folder.path()).filePath(storedRelativePath()),
                      QStringLiteral("the scan")));

    // Not a change, so not a move — and emphatically not the "already contains
    // an attachments folder" refusal, which would otherwise lock a vessel out
    // of the folder it is already using.
    QVERIFY2(roots.changeRoot(vesselId, folder.path(), &error), qPrintable(error));
    QCOMPARE(readFile(QDir(folder.path()).filePath(storedRelativePath())),
             QStringLiteral("the scan"));
}

void TestVesselAttachmentRoots::anAttachmentStoredBeforeTheAddendumStillResolves()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));

    // The installation's own folder, standing in for where everything went
    // before vessels had folders of their own.
    QTemporaryDir legacy;
    QVERIFY(legacy.isValid());
    ManagedFileStore::setRootPathOverride(legacy.path());

    const QString relative = storedRelativePath();
    QVERIFY(writeFile(QDir(legacy.path()).filePath(relative), QStringLiteral("the old scan")));

    // §2.2: with no folder configured at all, opening still works.
    QCOMPARE(readFile(roots.resolveForOpening(vesselId, relative)),
             QStringLiteral("the old scan"));

    // And it still works once a folder has been configured but this particular
    // document has not been re-attached into it — §2.6 declines to migrate it.
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QString error;
    QVERIFY(roots.changeRoot(vesselId, folder.path(), &error));

    QCOMPARE(readFile(roots.resolveForOpening(vesselId, relative)),
             QStringLiteral("the old scan"));

    // "Nothing attached" still resolves to nothing, never to a folder.
    QVERIFY(roots.resolveForOpening(vesselId, QString()).isEmpty());
}

QTEST_MAIN(TestVesselAttachmentRoots)
#include "tst_VesselAttachmentRoots.moc"
