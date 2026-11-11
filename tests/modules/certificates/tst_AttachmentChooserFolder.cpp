#include "modules/certificates/ui/AttachmentChooser.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

// housekeeping-round-spec §5: the attachment chooser opens where a document
// was last picked from, and falls back to Documents when that folder has gone
// or turns out to be the vessel's own managed folder.
//
// Only the "where to start" decision is tested. A modal QFileDialog cannot be
// driven from a test, so what the dialog itself does with the folder is not
// asserted here — what feeds the decision is, and that is the whole of what
// this round changed.
class TestAttachmentChooserFolder : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void withNothingRememberedItStartsAtDocuments();
    void aRememberedFolderThatStillExistsIsWhereItStarts();
    void aRememberedFolderThatHasGoneFallsBackToDocuments();
    void theVesselsOwnAttachmentFolderFallsBackToDocuments();
    void aFolderInsideTheVesselsAttachmentFolderFallsBackToDocuments();
    void aSimilarlyNamedNeighbourOfTheVesselFolderIsStillAllowed();
    void aChosenFileRemembersTheFolderHoldingIt();
    void aChosenFolderRemembersItsParent();
    void cancellingRemembersNothing();

private:
    QString pathFor(const QString& name) { return m_folder.filePath(name); }
    static QString documents()
    {
        return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    QTemporaryDir m_folder;
    QTemporaryDir m_settingsFolder;
};

void TestAttachmentChooserFolder::init()
{
    QVERIFY(m_folder.isValid());
    QVERIFY(m_settingsFolder.isValid());

    // Never the developer's real settings (CLAUDE.md §10.1) — the same
    // override, for the same reason, that the header-layout suite uses. One
    // file per test, so nothing one test remembers reaches the next.
    AttachmentChooser::setSettingsFileOverride(
        m_settingsFolder.filePath(QStringLiteral("%1.ini").arg(QTest::currentTestFunction())));
}

void TestAttachmentChooserFolder::cleanup()
{
    AttachmentChooser::setSettingsFileOverride(QString());
}

void TestAttachmentChooserFolder::withNothingRememberedItStartsAtDocuments()
{
    // Every user's first time, and the behaviour before this round.
    QCOMPARE(AttachmentChooser::startingFolder(pathFor(QStringLiteral("managed"))), documents());
}

void TestAttachmentChooserFolder::aRememberedFolderThatStillExistsIsWhereItStarts()
{
    const QString scans = pathFor(QStringLiteral("scans"));
    QVERIFY(QDir().mkpath(scans));

    AttachmentChooser::rememberChosenFile(scans + QStringLiteral("/certificate.pdf"));

    QCOMPARE(AttachmentChooser::startingFolder(pathFor(QStringLiteral("managed"))),
             QDir::cleanPath(scans));
}

void TestAttachmentChooserFolder::aRememberedFolderThatHasGoneFallsBackToDocuments()
{
    const QString scans = pathFor(QStringLiteral("removable"));
    QVERIFY(QDir().mkpath(scans));
    AttachmentChooser::rememberChosenFile(scans + QStringLiteral("/certificate.pdf"));

    // Renamed, deleted, or a memory stick pulled out. Qt would otherwise fall
    // back to the working directory, which is wherever the application was
    // started from.
    QVERIFY(QDir(scans).removeRecursively());

    QCOMPARE(AttachmentChooser::startingFolder(pathFor(QStringLiteral("managed"))), documents());
}

void TestAttachmentChooserFolder::theVesselsOwnAttachmentFolderFallsBackToDocuments()
{
    // The managed folder is the one place a source must never be picked from:
    // copying a file onto itself. A remembered folder can become the managed
    // folder the moment a vessel is pointed at somewhere documents were once
    // picked from.
    const QString managed = pathFor(QStringLiteral("fleet/VESSEL ONE"));
    QVERIFY(QDir().mkpath(managed));

    AttachmentChooser::rememberChosenFile(managed + QStringLiteral("/certificate.pdf"));

    QCOMPARE(AttachmentChooser::startingFolder(managed), documents());
}

void TestAttachmentChooserFolder::aFolderInsideTheVesselsAttachmentFolderFallsBackToDocuments()
{
    const QString managed = pathFor(QStringLiteral("fleet/VESSEL ONE"));
    const QString inside  = managed + QStringLiteral("/Archive");
    QVERIFY(QDir().mkpath(inside));

    AttachmentChooser::rememberChosenFile(inside + QStringLiteral("/old.pdf"));

    QCOMPARE(AttachmentChooser::startingFolder(managed), documents());
}

void TestAttachmentChooserFolder::aSimilarlyNamedNeighbourOfTheVesselFolderIsStillAllowed()
{
    // The check compares whole path segments, so a folder whose name merely
    // starts with the managed folder's name is not "inside" it.
    const QString managed   = pathFor(QStringLiteral("fleet/ships"));
    const QString neighbour = pathFor(QStringLiteral("fleet/ships-scans"));
    QVERIFY(QDir().mkpath(managed));
    QVERIFY(QDir().mkpath(neighbour));

    AttachmentChooser::rememberChosenFile(neighbour + QStringLiteral("/certificate.pdf"));

    QCOMPARE(AttachmentChooser::startingFolder(managed), QDir::cleanPath(neighbour));
}

void TestAttachmentChooserFolder::aChosenFileRemembersTheFolderHoldingIt()
{
    const QString scans = pathFor(QStringLiteral("scans"));
    QVERIFY(QDir().mkpath(scans));

    AttachmentChooser::rememberChosenFile(scans + QStringLiteral("/IOPP.pdf"));

    // The folder, not the file: reopening at the file itself would single one
    // document out, which is not what "where I was last working" means.
    QCOMPARE(AttachmentChooser::startingFolder(QString()), QDir::cleanPath(scans));
}

void TestAttachmentChooserFolder::aChosenFolderRemembersItsParent()
{
    const QString parent = pathFor(QStringLiteral("scans"));
    const QString chosen = parent + QStringLiteral("/IOPP");
    QVERIFY(QDir().mkpath(chosen));

    AttachmentChooser::rememberChosenFolder(chosen);

    // The parent, so the folder just chosen is visible among its neighbours
    // next time rather than being the one folder shown from the inside.
    QCOMPARE(AttachmentChooser::startingFolder(QString()), QDir::cleanPath(parent));
}

void TestAttachmentChooserFolder::cancellingRemembersNothing()
{
    const QString scans = pathFor(QStringLiteral("scans"));
    QVERIFY(QDir().mkpath(scans));
    AttachmentChooser::rememberChosenFile(scans + QStringLiteral("/IOPP.pdf"));

    // What a cancelled dialog hands back. Both chooseFile() and chooseFolder()
    // pass it straight to these, so this is the guard that keeps a Cancel from
    // clearing or moving what was remembered.
    AttachmentChooser::rememberChosenFile(QString());
    AttachmentChooser::rememberChosenFolder(QString());

    QCOMPARE(AttachmentChooser::startingFolder(QString()), QDir::cleanPath(scans));
}

QTEST_MAIN(TestAttachmentChooserFolder)
#include "tst_AttachmentChooserFolder.moc"
