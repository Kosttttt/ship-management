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

// certificate-attachment-addendum-spec §2 and §7 cases 1 to 4, as amended by
// certificate-attachment-addendum-2-spec §3: the layout under a vessel's folder
// is flat, so the "already in use by another vessel" test is now a marker file
// (§3.3) and a folder change moves the folder's own contents rather than one
// subtree.
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
    void settingAFolderWritesOnlyItsMarker();                 // case 2, §3.3
    void changingTheFolderMovesTheDocumentsAndKeepsThePath(); // case 3
    void aFolderMarkedForAnotherVesselIsRefused();            // case 4, §3.3
    void aFolderThatDoesNotExistIsRefused();
    void choosingTheSameFolderAgainIsAcceptedAndMovesNothing();
    void aFolderThisVesselUsedBeforeIsAcceptedAgain();        // §3.3

    // §2.2 — opening is never blocked.
    void anAttachmentStoredBeforeTheAddendumStillResolves();
    void rootHoldingNamesTheFolderTheFileIsActuallyIn();

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
    // is in force. Flat now (§3.1): the name and nothing above it.
    static QString storedRelativePath()
    {
        return QStringLiteral("15D_Load_Line_2031-02-04.pdf");
    }

    static QString markerIn(const QString& folder)
    {
        return QDir(folder).filePath(VesselAttachmentRoots::kMarkerFileName);
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

// §7 case 2, as §3.3 changes it: the one thing configuring a folder creates is
// the marker that says whose folder it is. Nothing else — no "attachments"
// subfolder, because there is no longer such a thing (§3.1).
void TestVesselAttachmentRoots::settingAFolderWritesOnlyItsMarker()
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

    // The marker, with this vessel's id in it and nothing else.
    QVERIFY2(QFileInfo::exists(markerIn(folder.path())),
             "configuring a folder must claim it, or a second vessel could be pointed at it");
    QCOMPARE(readFile(markerIn(folder.path())), vesselId);

    // And genuinely nothing else: the folder the user chose is the folder the
    // documents go in, not the parent of one the application invents.
    const QStringList entries = ManagedFileStore::entriesIn(folder.path());
    QCOMPARE(entries, QStringList({VesselAttachmentRoots::kMarkerFileName}));
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

    // A document and an archived one, where real attachments would be (§3.1,
    // §3.2).
    const QString relative = storedRelativePath();
    QVERIFY(writeFile(QDir(first.path()).filePath(relative), QStringLiteral("the scan")));
    QVERIFY(writeFile(QDir(first.path()).filePath(QStringLiteral("archive/old.pdf")),
                      QStringLiteral("the retired one")));

    QVERIFY2(roots.changeRoot(vesselId, second.path(), &error), qPrintable(error));

    // §7 case 3: the file physically relocated, and so did the archive...
    QCOMPARE(readFile(QDir(second.path()).filePath(relative)), QStringLiteral("the scan"));
    QCOMPARE(readFile(QDir(second.path()).filePath(QStringLiteral("archive/old.pdf"))),
             QStringLiteral("the retired one"));
    QVERIFY2(!QFileInfo::exists(QDir(first.path()).filePath(relative)),
             "a move leaves nothing behind, or the two copies drift apart");

    // ...the marker came with them, so the old folder no longer claims this
    // vessel and the new one does...
    QVERIFY(!QFileInfo::exists(markerIn(first.path())));
    QCOMPARE(readFile(markerIn(second.path())), vesselId);

    // ...and the stored relative path did not have to change, because only the
    // root changed. That is CLAUDE.md §6.9 doing its job.
    QCOMPARE(roots.resolveForOpening(vesselId, relative),
             QDir(QFileInfo(second.path()).absoluteFilePath()).filePath(relative));
}

// §7 case 4 under §3.3's mechanism: the marker, not an "attachments" folder, is
// what identifies a folder as somebody else's.
void TestVesselAttachmentRoots::aFolderMarkedForAnotherVesselIsRefused()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString mine = seedVessel(db, QStringLiteral("MV Mine"), QStringLiteral("9074729"));
    const QString theirs =
        seedVessel(db, QStringLiteral("MV Theirs"), QStringLiteral("9245807"));

    QTemporaryDir myFolder;
    QTemporaryDir theirFolder;
    QVERIFY(myFolder.isValid());
    QVERIFY(theirFolder.isValid());

    QString error;
    QVERIFY(roots.changeRoot(mine, myFolder.path(), &error));
    QVERIFY(writeFile(QDir(myFolder.path()).filePath(storedRelativePath()),
                      QStringLiteral("ours")));

    // The other vessel really configures its folder, so the marker is written
    // by the code under test rather than faked.
    QVERIFY2(roots.changeRoot(theirs, theirFolder.path(), &error), qPrintable(error));
    QVERIFY(writeFile(QDir(theirFolder.path()).filePath(QStringLiteral("theirs.pdf")),
                      QStringLiteral("theirs")));

    QVERIFY2(!roots.changeRoot(mine, theirFolder.path(), &error),
             "a folder another vessel has claimed must be refused, not merged into");
    QVERIFY2(error.contains(QStringLiteral("another vessel")), qPrintable(error));

    // §7 case 4: nothing moved and nothing changed, on either side.
    QCOMPARE(readFile(QDir(myFolder.path()).filePath(storedRelativePath())),
             QStringLiteral("ours"));
    QCOMPARE(readFile(QDir(theirFolder.path()).filePath(QStringLiteral("theirs.pdf"))),
             QStringLiteral("theirs"));
    QCOMPARE(readFile(markerIn(theirFolder.path())), theirs);
    QCOMPARE(roots.rootForWriting(mine, &error), QFileInfo(myFolder.path()).absoluteFilePath());
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

    // addendum 2 §9 case 5: not a change, so not a move — and emphatically not
    // the marker refusal, which would otherwise lock a vessel out of the
    // folder it is already using.
    QVERIFY2(roots.changeRoot(vesselId, folder.path(), &error), qPrintable(error));
    QCOMPARE(readFile(QDir(folder.path()).filePath(storedRelativePath())),
             QStringLiteral("the scan"));
}

// §3.3: a marker naming *this* vessel is not a refusal. A vessel moved away
// from a folder and then back again would otherwise be locked out of it by its
// own leftover marker.
void TestVesselAttachmentRoots::aFolderThisVesselUsedBeforeIsAcceptedAgain()
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
    QVERIFY(roots.changeRoot(vesselId, second.path(), &error));

    // The marker put back in the first folder by hand, as one left behind by a
    // part-finished move would be. It names this vessel, so going back is
    // allowed.
    QVERIFY(writeFile(markerIn(first.path()), vesselId));

    QVERIFY2(roots.changeRoot(vesselId, first.path(), &error), qPrintable(error));
    QCOMPARE(roots.rootForWriting(vesselId, &error), QFileInfo(first.path()).absoluteFilePath());
}

void TestVesselAttachmentRoots::anAttachmentStoredBeforeTheAddendumStillResolves()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));

    // The installation's own folder, standing in for where everything went
    // before vessels had folders of their own. The nested shape is deliberate:
    // this is a path stored under the layout addendum 2 §3.1 replaced, and it
    // still has to resolve.
    QTemporaryDir legacy;
    QVERIFY(legacy.isValid());
    ManagedFileStore::setRootPathOverride(legacy.path());

    const QString relative = QStringLiteral("attachments/c1/15D_Load_Line_2031-02-04.pdf");
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

// The root behind resolveForOpening(), exposed because deleting a file has to
// act on where it really is rather than where a new one would go
// (addendum 2 §4 and §5).
void TestVesselAttachmentRoots::rootHoldingNamesTheFolderTheFileIsActuallyIn()
{
    QSqlDatabase          db = database();
    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    const QString vesselId =
        seedVessel(db, QStringLiteral("MV Example"), QStringLiteral("9074729"));

    QTemporaryDir legacy;
    QTemporaryDir folder;
    QVERIFY(legacy.isValid());
    QVERIFY(folder.isValid());
    ManagedFileStore::setRootPathOverride(legacy.path());

    QString error;
    QVERIFY(roots.changeRoot(vesselId, folder.path(), &error));

    const QString relative = storedRelativePath();

    // Nowhere yet: an empty answer, so a delete does nothing at all rather
    // than guess at a folder.
    QVERIFY(roots.rootHolding(vesselId, relative).isEmpty());
    QVERIFY(roots.rootHolding(vesselId, QString()).isEmpty());

    // Only in the pre-addendum folder: that is where a delete has to act.
    QVERIFY(writeFile(QDir(legacy.path()).filePath(relative), QStringLiteral("the old one")));
    QCOMPARE(roots.rootHolding(vesselId, relative),
             QFileInfo(legacy.path()).absoluteFilePath());

    // In the vessel's own folder too: that one wins.
    QVERIFY(writeFile(QDir(folder.path()).filePath(relative), QStringLiteral("the current one")));
    QCOMPARE(roots.rootHolding(vesselId, relative),
             QFileInfo(folder.path()).absoluteFilePath());
}

QTEST_MAIN(TestVesselAttachmentRoots)
#include "tst_VesselAttachmentRoots.moc"
