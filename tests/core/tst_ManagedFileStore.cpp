#include "core/Database.h"
#include "core/ManagedFileStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// The one subfolder the flattened layout keeps
// (certificate-attachment-addendum-2-spec §3.2). This class is deliberately
// certificate-agnostic, so the name is a parameter it is handed; the constant
// lives here rather than being reached for out of the certificates module.
const QString kArchive = QStringLiteral("archive");

} // namespace

// certificate-attachment-spec §5: exercised directly against a QTemporaryDir
// standing in for the managed data folder. No database and no certificate is
// involved — that separation is the point of the class.
class TestManagedFileStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // The §4 requirement the whole feature rests on.
    void rootPathComesFromTheDatabaseFolder();
    void theOverrideReplacesTheRootAndClearsAgain();

    void copiesAFileInAndReportsARelativePath();
    void copiesAWholeFolderKeepingTheNamesInsideIt();
    void aCollisionEarnsASuffixBeforeTheExtension();
    void aMissingSourceFailsAndLeavesNothingBehind();

    void archivingAFileMovesItAndStampsTheName();
    void archivingAFolderMovesTheWholeThing();
    void archivingSomethingThatIsNotThereFails();

    // certificate-attachment-addendum-2-spec §2 and §9 case 1.
    void anArchivedFileKeepsItsExtension();
    void aSecondArchiveInTheSameSecondStillKeepsItsExtension();

    // §4 and §5: a genuine delete, with the guards that make it safe.
    void deletingRemovesTheFileForGood();
    void deletingAFolderTakesTheWholeThing();
    void deletingNothingIsRefusedRatherThanDeletingTheRoot();
    void deletingSomethingAlreadyGoneIsSuccess();

    void resolveAbsoluteJoinsTheRoot();
    void anEmptyRelativePathResolvesToNothing();
    void theSameRelativePathResolvesUnderADifferentRoot();

    // certificate-attachment-addendum-spec §2.4 and §2.5.
    void theOverrideIsReadableSoTheVesselLookupCanHonourIt();
    void movingATreeTakesEverythingInsideIt();
    void movingOntoSomethingThatExistsIsRefused();
    void movingSomethingThatIsNotThereFails();

    // addendum 2 §3.1: a vessel's folder change now moves the folder's own
    // entries, because there is no longer a subtree to single out.
    void movingNamedEntriesTakesHiddenOnesToo();
    void aNameAlreadyTakenRefusesTheWholeMoveAndTouchesNothing();
    void entriesInListsHiddenFilesAndIgnoresAMissingFolder();

private:
    // A folder of its own per test, inside the one QTemporaryDir this object
    // holds. QTemporaryDir is a member rather than something init() recreates,
    // so without this every test would inherit the files the last one left —
    // an archive/ folder from one test made three of the archive assertions
    // below count two files where they had written one.
    QString root() const { return m_perTestRoot; }

    // Writes a file with known contents, so a copy can be proved to be a copy
    // rather than an empty file of the right name.
    static bool writeFile(const QString& absolutePath, const QString& contents)
    {
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

    // A folder somewhere else on disk, standing in for the user's own machine.
    QTemporaryDir m_source;
    // The managed data folder, with one subfolder per test inside it.
    QTemporaryDir m_root;
    QString       m_perTestRoot;
};

void TestManagedFileStore::init()
{
    QVERIFY(m_source.isValid());
    QVERIFY(m_root.isValid());

    m_perTestRoot = QDir(m_root.path()).filePath(QTest::currentTestFunction());
    QVERIFY(QDir().mkpath(m_perTestRoot));

    ManagedFileStore::setRootPathOverride(m_perTestRoot);
}

void TestManagedFileStore::cleanup()
{
    // Process-wide state: left set, it would follow the next test into a
    // folder that no longer exists.
    ManagedFileStore::setRootPathOverride(QString());
}

// §4, and the single most important assertion in this suite. Two independent
// resolutions of "the per-user data folder" are not guaranteed to be the same
// folder on this machine — that is exactly how the step-7 incident in
// CLAUDE.md §10.1 happened — so the files must go where the database already
// is, by construction rather than by coincidence.
void TestManagedFileStore::rootPathComesFromTheDatabaseFolder()
{
    ManagedFileStore::setRootPathOverride(QString());

    const QString expected = QFileInfo(Database::defaultFilePath()).absolutePath();

    QVERIFY(!expected.isEmpty());
    QCOMPARE(ManagedFileStore::rootPath(), expected);

    // Stated the other way round as well: the database file really is inside
    // the folder the attachments go into, so copying the folder takes both.
    QVERIFY(Database::defaultFilePath().startsWith(ManagedFileStore::rootPath()));
}

void TestManagedFileStore::theOverrideReplacesTheRootAndClearsAgain()
{
    ManagedFileStore::setRootPathOverride(m_root.path());
    QCOMPARE(ManagedFileStore::rootPath(), QFileInfo(m_root.path()).absoluteFilePath());

    ManagedFileStore::setRootPathOverride(QString());
    QCOMPARE(ManagedFileStore::rootPath(),
             QFileInfo(Database::defaultFilePath()).absolutePath());
}

void TestManagedFileStore::copiesAFileInAndReportsARelativePath()
{
    const QString source = QDir(m_source.path()).filePath(QStringLiteral("scan.pdf"));
    QVERIFY(writeFile(source, QStringLiteral("page one")));

    QString relative;
    QString error;
    // An empty destination means the root folder itself, which is where a
    // vessel's documents go since addendum 2 §3.1.
    QVERIFY2(ManagedFileStore::copyInto(root(), source, QString(),
                                        QStringLiteral("15D_Load_Line_2027-06-01.pdf"), &relative,
                                        &error),
             qPrintable(error));

    // Relative, with forward slashes: this is the string the database stores
    // (CLAUDE.md §6.9). Just the filename now, with nothing above it.
    QCOMPARE(relative, QStringLiteral("15D_Load_Line_2027-06-01.pdf"));
    QVERIFY(!QDir::isAbsolutePath(relative));

    const QString absolute = QDir(root()).filePath(relative);
    QVERIFY(QFileInfo::exists(absolute));
    QCOMPARE(readFile(absolute), QStringLiteral("page one"));

    // The original is a copy source, not a move source: the user keeps their
    // own file.
    QVERIFY(QFileInfo::exists(source));
}

void TestManagedFileStore::copiesAWholeFolderKeepingTheNamesInsideIt()
{
    const QString sourceDir = QDir(m_source.path()).filePath(QStringLiteral("survey pack"));
    QVERIFY(QDir().mkpath(QDir(sourceDir).filePath(QStringLiteral("photos"))));
    QVERIFY(writeFile(QDir(sourceDir).filePath(QStringLiteral("report.pdf")),
                      QStringLiteral("the report")));
    QVERIFY(writeFile(QDir(sourceDir).filePath(QStringLiteral("photos/bow.jpg")),
                      QStringLiteral("a photo")));

    QString relative;
    QString error;
    QVERIFY2(ManagedFileStore::copyInto(root(), sourceDir, QString(),
                                        QStringLiteral("15D_Load_Line_2027-06-01"), &relative,
                                        &error),
             qPrintable(error));

    QCOMPARE(relative, QStringLiteral("15D_Load_Line_2027-06-01"));

    const QDir copied(QDir(root()).filePath(relative));
    QVERIFY(copied.exists());

    // §2: the folder is renamed, the files inside it are not — renaming
    // several arbitrary files to one name would collide or destroy
    // information.
    QCOMPARE(readFile(copied.filePath(QStringLiteral("report.pdf"))),
             QStringLiteral("the report"));
    QCOMPARE(readFile(copied.filePath(QStringLiteral("photos/bow.jpg"))),
             QStringLiteral("a photo"));
}

void TestManagedFileStore::aCollisionEarnsASuffixBeforeTheExtension()
{
    const QString source = QDir(m_source.path()).filePath(QStringLiteral("scan.pdf"));
    QVERIFY(writeFile(source, QStringLiteral("page one")));

    QString first;
    QString error;
    QVERIFY2(ManagedFileStore::copyInto(root(), source, QString(),
                                        QStringLiteral("15D_ShortTerm_2027-03-01.pdf"), &first,
                                        &error),
             qPrintable(error));

    QString second;
    QVERIFY2(ManagedFileStore::copyInto(root(), source, QString(),
                                        QStringLiteral("15D_ShortTerm_2027-03-01.pdf"), &second,
                                        &error),
             qPrintable(error));

    QCOMPARE(first, QStringLiteral("15D_ShortTerm_2027-03-01.pdf"));
    // The suffix goes before the extension, or the operating system stops
    // recognising the file.
    QCOMPARE(second, QStringLiteral("15D_ShortTerm_2027-03-01_2.pdf"));

    QVERIFY(QFileInfo::exists(QDir(root()).filePath(first)));
    QVERIFY(QFileInfo::exists(QDir(root()).filePath(second)));
}

void TestManagedFileStore::aMissingSourceFailsAndLeavesNothingBehind()
{
    const QString missing = QDir(m_source.path()).filePath(QStringLiteral("not-there.pdf"));

    QString relative;
    QString error;
    QVERIFY(!ManagedFileStore::copyInto(root(), missing, QString(),
                                        QStringLiteral("name.pdf"), &relative, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(relative.isEmpty());

    QVERIFY2(!QFileInfo::exists(QDir(root()).filePath(QStringLiteral("name.pdf"))),
             "a failed copy must leave nothing for a row to point at");
}

void TestManagedFileStore::archivingAFileMovesItAndStampsTheName()
{
    const QString current = QDir(root()).filePath(QStringLiteral("current.pdf"));
    QVERIFY(writeFile(current, QStringLiteral("the old document")));

    QString error;
    QVERIFY2(ManagedFileStore::moveToArchive(root(), QStringLiteral("current.pdf"),
                                             kArchive, &error),
             qPrintable(error));

    // Moved, not copied: the vessel's folder is meant to hold exactly what is
    // current (§4).
    QVERIFY(!QFileInfo::exists(current));

    const QDir archive(QDir(root()).filePath(kArchive));
    const QStringList archived = archive.entryList(QDir::Files);
    QCOMPARE(archived.size(), 1);

    // §4: the marker exists only to make the name unique, and is not meant to
    // be read — but the original name has to survive in front of it, and
    // addendum 2 §2 adds that the extension has to survive after it.
    QVERIFY2(archived.first().startsWith(QStringLiteral("current__archived-")),
             qPrintable(archived.first()));

    // §9 case 8: nothing is ever actually deleted.
    QCOMPARE(readFile(archive.filePath(archived.first())), QStringLiteral("the old document"));
}

void TestManagedFileStore::archivingAFolderMovesTheWholeThing()
{
    const QString folder = QDir(root()).filePath(QStringLiteral("pack"));
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("inside.txt")),
                      QStringLiteral("still here")));

    QString error;
    QVERIFY2(ManagedFileStore::moveToArchive(root(), QStringLiteral("pack"), kArchive, &error),
             qPrintable(error));

    QVERIFY(!QFileInfo::exists(folder));

    const QDir        archive(QDir(root()).filePath(kArchive));
    const QStringList archived = archive.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(archived.size(), 1);
    QVERIFY(archived.first().startsWith(QStringLiteral("pack__archived-")));
    QCOMPARE(readFile(QDir(archive.filePath(archived.first())).filePath(
                 QStringLiteral("inside.txt"))),
             QStringLiteral("still here"));
}

void TestManagedFileStore::archivingSomethingThatIsNotThereFails()
{
    QString error;
    QVERIFY(!ManagedFileStore::moveToArchive(root(), QStringLiteral("ghost.pdf"), kArchive,
                                             &error));
    QVERIFY(!error.isEmpty());

    // An empty path is a caller mistake, not "nothing to do": the caller skips
    // archiving itself when there is no current attachment.
    QVERIFY(!ManagedFileStore::moveToArchive(root(), QString(), kArchive, &error));
}

// addendum 2 §2, the bug this fixed: the marker went on after the extension,
// so the archived copy was a ".pdf__archived-20261102T111954Z" that nothing
// would open from a file browser.
void TestManagedFileStore::anArchivedFileKeepsItsExtension()
{
    const QString name = QStringLiteral("15D_Load_Line_2031-02-04.pdf");
    QVERIFY(writeFile(QDir(root()).filePath(name), QStringLiteral("the old document")));

    QString error;
    QVERIFY2(ManagedFileStore::moveToArchive(root(), name, kArchive, &error), qPrintable(error));

    const QDir        archive(QDir(root()).filePath(kArchive));
    const QStringList archived = archive.entryList(QDir::Files);
    QCOMPARE(archived.size(), 1);

    const QString archivedName = archived.first();

    // The three things §2 asks for at once: the original name in front, the
    // marker in the middle, and the extension still last.
    QVERIFY2(archivedName.startsWith(QStringLiteral("15D_Load_Line_2031-02-04__archived-")),
             qPrintable(archivedName));
    QVERIFY2(archivedName.endsWith(QStringLiteral(".pdf")), qPrintable(archivedName));
    QCOMPARE(QFileInfo(archivedName).suffix(), QStringLiteral("pdf"));

    // And it is the same document, not a renamed empty file.
    QCOMPARE(readFile(archive.filePath(archivedName)), QStringLiteral("the old document"));
}

// The uniqueness index has to obey the same rule: two archives inside one
// second must not produce "..._archived-X.pdf_2".
void TestManagedFileStore::aSecondArchiveInTheSameSecondStillKeepsItsExtension()
{
    QString error;
    for (int round = 0; round < 2; ++round) {
        QVERIFY(writeFile(QDir(root()).filePath(QStringLiteral("scan.pdf")),
                          QStringLiteral("round %1").arg(round)));
        QVERIFY2(ManagedFileStore::moveToArchive(root(), QStringLiteral("scan.pdf"), kArchive,
                                                 &error),
                 qPrintable(error));
    }

    const QDir        archive(QDir(root()).filePath(kArchive));
    const QStringList archived = archive.entryList(QDir::Files);
    QCOMPARE(archived.size(), 2);

    for (const QString& name : archived) {
        QVERIFY2(name.endsWith(QStringLiteral(".pdf")), qPrintable(name));
        QVERIFY2(name.startsWith(QStringLiteral("scan__archived-")), qPrintable(name));
    }
}

// §4 and §5: a genuine delete, with no archive copy anywhere.
void TestManagedFileStore::deletingRemovesTheFileForGood()
{
    const QString name = QStringLiteral("15D_Load_Line_2031-02-04.pdf");
    QVERIFY(writeFile(QDir(root()).filePath(name), QStringLiteral("a mistake")));

    QString error;
    QVERIFY2(ManagedFileStore::deleteAt(root(), name, &error), qPrintable(error));

    QVERIFY(!QFileInfo::exists(QDir(root()).filePath(name)));
    QVERIFY2(!QFileInfo::exists(QDir(root()).filePath(kArchive)),
             "a delete must not quietly archive what it was asked to destroy");
}

void TestManagedFileStore::deletingAFolderTakesTheWholeThing()
{
    const QString folder = QDir(root()).filePath(QStringLiteral("pack"));
    QVERIFY(QDir().mkpath(QDir(folder).filePath(QStringLiteral("photos"))));
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("photos/bow.jpg")),
                      QStringLiteral("a photo")));

    QString error;
    QVERIFY2(ManagedFileStore::deleteAt(root(), QStringLiteral("pack"), &error),
             qPrintable(error));
    QVERIFY(!QFileInfo::exists(folder));
}

// The guard that matters most here. CLAUDE.md §10.1 exists because a command
// that looked correct destroyed a real folder, and "nothing attached" is
// exactly the input that would otherwise name the whole vessel folder.
void TestManagedFileStore::deletingNothingIsRefusedRatherThanDeletingTheRoot()
{
    QVERIFY(writeFile(QDir(root()).filePath(QStringLiteral("keep.pdf")),
                      QStringLiteral("not yours to delete")));

    QString error;
    QVERIFY2(!ManagedFileStore::deleteAt(root(), QString(), &error),
             "an empty path must never resolve to the vessel's whole folder");
    QVERIFY(!error.isEmpty());

    // The root under another spelling, climbing out of it, and an absolute
    // path are all refused for the same reason: QDir::filePath() would hand
    // each of them back as a folder nobody asked to have deleted.
    QVERIFY(!ManagedFileStore::deleteAt(root(), QStringLiteral("."), &error));
    QVERIFY(!ManagedFileStore::deleteAt(root(), QStringLiteral("keep.pdf/.."), &error));
    QVERIFY(!ManagedFileStore::deleteAt(root(), QStringLiteral("../.."), &error));
    QVERIFY(!ManagedFileStore::deleteAt(root(), root(), &error));

    // Nothing in the folder was touched by any of the three.
    QCOMPARE(readFile(QDir(root()).filePath(QStringLiteral("keep.pdf"))),
             QStringLiteral("not yours to delete"));
    QVERIFY(QDir(root()).exists());
}

void TestManagedFileStore::deletingSomethingAlreadyGoneIsSuccess()
{
    // The caller wanted it not to be there, and it is not. §4 deletes a file
    // whose row named it; a file somebody removed by hand first is not an
    // error to report back to them.
    QString error;
    QVERIFY(ManagedFileStore::deleteAt(root(), QStringLiteral("never-existed.pdf"), &error));
    QVERIFY(error.isEmpty());
}

void TestManagedFileStore::resolveAbsoluteJoinsTheRoot()
{
    const QString relative = QStringLiteral("15D_Load_Line_2027-06-01.pdf");
    QCOMPARE(ManagedFileStore::resolveAbsolute(root(), relative), QDir(root()).filePath(relative));
}

void TestManagedFileStore::anEmptyRelativePathResolvesToNothing()
{
    // "There is no attachment" must never resolve to the managed folder
    // itself, which something would then helpfully open.
    QCOMPARE(ManagedFileStore::resolveAbsolute(root(), QString()), QString());
}

// §9 case 9: the concrete proof that CLAUDE.md §6.9 holds for this feature.
// The same stored string points at the right file on a second machine, because
// what is stored is relative and only the root differs.
//
// Since the attachment addendum the root is a parameter rather than process
// state, so this is now a direct statement rather than something demonstrated
// by switching an override: two roots, one relative path, two real files.
void TestManagedFileStore::theSameRelativePathResolvesUnderADifferentRoot()
{
    const QString relative = QStringLiteral("15D_Load_Line_2027-06-01.pdf");

    QVERIFY(QDir().mkpath(QFileInfo(QDir(root()).filePath(relative)).absolutePath()));
    QVERIFY(writeFile(QDir(root()).filePath(relative), QStringLiteral("the ship copy")));

    const QString onTheShip = ManagedFileStore::resolveAbsolute(root(), relative);
    QVERIFY(QFileInfo::exists(onTheShip));

    // A second folder standing in for the office machine, with the same
    // subtree copied into it.
    QTemporaryDir office;
    QVERIFY(office.isValid());
    QVERIFY(QDir().mkpath(QFileInfo(QDir(office.path()).filePath(relative)).absolutePath()));
    QVERIFY(writeFile(QDir(office.path()).filePath(relative), QStringLiteral("the office copy")));

    const QString inTheOffice = ManagedFileStore::resolveAbsolute(office.path(), relative);
    QVERIFY(inTheOffice != onTheShip);
    QVERIFY(QFileInfo::exists(inTheOffice));
    QCOMPARE(readFile(inTheOffice), QStringLiteral("the office copy"));
}

// certificate-attachment-addendum-spec §2.4: the override is still the one
// thing that can replace the installation's own folder, and clearing it still
// puts it back. The addendum changes which folder attachments go into, not
// this.
void TestManagedFileStore::theOverrideIsReadableSoTheVesselLookupCanHonourIt()
{
    ManagedFileStore::setRootPathOverride(m_root.path());
    QCOMPARE(ManagedFileStore::rootPathOverride(),
             QFileInfo(m_root.path()).absoluteFilePath());

    ManagedFileStore::setRootPathOverride(QString());
    QVERIFY2(ManagedFileStore::rootPathOverride().isEmpty(),
             "with nothing overridden, VesselAttachmentRoots must fall through to the vessel");
}

// §2.5, the fast path: both folders on one volume, so a rename does it.
void TestManagedFileStore::movingATreeTakesEverythingInsideIt()
{
    const QString source = QDir(root()).filePath(QStringLiteral("subtree"));
    QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("inner"))));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("current.pdf")),
                      QStringLiteral("the document")));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("inner/short.pdf")),
                      QStringLiteral("the short-term one")));

    QTemporaryDir newRoot;
    QVERIFY(newRoot.isValid());
    const QString target = QDir(newRoot.path()).filePath(QStringLiteral("subtree"));

    QString error;
    QVERIFY2(ManagedFileStore::moveTree(source, target, &error), qPrintable(error));

    // Moved, not copied: a vessel's documents exist in one place.
    QVERIFY2(!QFileInfo::exists(source), "the old location must not keep a second copy");

    QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("current.pdf"))),
             QStringLiteral("the document"));
    QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("inner/short.pdf"))),
             QStringLiteral("the short-term one"));
}

// §2.5 again: a destination that already holds something is refused rather
// than merged, and the source is left exactly as it was.
void TestManagedFileStore::movingOntoSomethingThatExistsIsRefused()
{
    const QString source = QDir(root()).filePath(QStringLiteral("ours"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("mine.pdf")), QStringLiteral("mine")));

    QTemporaryDir newRoot;
    QVERIFY(newRoot.isValid());
    const QString target = QDir(newRoot.path()).filePath(QStringLiteral("ours"));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("theirs.pdf")),
                      QStringLiteral("theirs")));

    QString error;
    QVERIFY(!ManagedFileStore::moveTree(source, target, &error));
    QVERIFY(!error.isEmpty());

    // Both sides untouched: nothing was pooled, nothing was lost.
    QCOMPARE(readFile(QDir(source).filePath(QStringLiteral("mine.pdf"))),
             QStringLiteral("mine"));
    QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("theirs.pdf"))),
             QStringLiteral("theirs"));
}

void TestManagedFileStore::movingSomethingThatIsNotThereFails()
{
    QTemporaryDir newRoot;
    QVERIFY(newRoot.isValid());

    const QString missing = QDir(root()).filePath(QStringLiteral("no-such-subtree"));
    QVERIFY(!QFileInfo::exists(missing));

    QString error;
    QVERIFY(!ManagedFileStore::moveTree(
        missing, QDir(newRoot.path()).filePath(QStringLiteral("anywhere")), &error));
    QVERIFY(!error.isEmpty());
}

// addendum 2 §3.1: a vessel's folder change moves the folder's own entries now.
// Hidden entries are listed and moved like any other, because a document can
// perfectly well be one; which of them to leave out is the caller's decision,
// and VesselAttachmentRoots does leave its own marker file out.
void TestManagedFileStore::movingNamedEntriesTakesHiddenOnesToo()
{
    const QString source = QDir(root()).filePath(QStringLiteral("vessel-a"));
    QVERIFY(QDir().mkpath(QDir(source).filePath(kArchive)));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("15D_Load_Line.pdf")),
                      QStringLiteral("the document")));
    QVERIFY(writeFile(QDir(source).filePath(kArchive + QStringLiteral("/old.pdf")),
                      QStringLiteral("the retired one")));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral(".ship-management-vessel")),
                      QStringLiteral("vessel-1")));

    const QStringList names = ManagedFileStore::entriesIn(source);
    QCOMPARE(names.size(), 3);

    QTemporaryDir target;
    QVERIFY(target.isValid());

    QString error;
    QVERIFY2(ManagedFileStore::moveNamedEntries(source, target.path(), names, &error),
             qPrintable(error));

    QCOMPARE(readFile(QDir(target.path()).filePath(QStringLiteral("15D_Load_Line.pdf"))),
             QStringLiteral("the document"));
    QCOMPARE(readFile(QDir(target.path()).filePath(kArchive + QStringLiteral("/old.pdf"))),
             QStringLiteral("the retired one"));
    QCOMPARE(readFile(QDir(target.path()).filePath(QStringLiteral(".ship-management-vessel"))),
             QStringLiteral("vessel-1"));

    // The folder itself stays; only its contents went.
    QVERIFY(QDir(source).exists());
    QVERIFY(ManagedFileStore::entriesIn(source).isEmpty());
}

void TestManagedFileStore::aNameAlreadyTakenRefusesTheWholeMoveAndTouchesNothing()
{
    const QString source = QDir(root()).filePath(QStringLiteral("vessel-b"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("first.pdf")),
                      QStringLiteral("ours, first")));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("second.pdf")),
                      QStringLiteral("ours, second")));

    QTemporaryDir target;
    QVERIFY(target.isValid());
    QVERIFY(writeFile(QDir(target.path()).filePath(QStringLiteral("second.pdf")),
                      QStringLiteral("somebody else's")));

    QString error;
    QVERIFY2(!ManagedFileStore::moveNamedEntries(source, target.path(),
                                                 ManagedFileStore::entriesIn(source), &error),
             "one taken name must refuse the whole move");
    QVERIFY2(error.contains(QStringLiteral("second.pdf")), qPrintable(error));

    // Checked before anything moved, so "first.pdf" never left either: a
    // vessel's documents must not end up split across two folders.
    QCOMPARE(readFile(QDir(source).filePath(QStringLiteral("first.pdf"))),
             QStringLiteral("ours, first"));
    QCOMPARE(readFile(QDir(source).filePath(QStringLiteral("second.pdf"))),
             QStringLiteral("ours, second"));
    QCOMPARE(readFile(QDir(target.path()).filePath(QStringLiteral("second.pdf"))),
             QStringLiteral("somebody else's"));
    QVERIFY(!QFileInfo::exists(QDir(target.path()).filePath(QStringLiteral("first.pdf"))));
}

void TestManagedFileStore::entriesInListsHiddenFilesAndIgnoresAMissingFolder()
{
    QVERIFY(ManagedFileStore::entriesIn(QDir(root()).filePath(QStringLiteral("nope"))).isEmpty());
    QVERIFY(ManagedFileStore::entriesIn(QString()).isEmpty());

    const QString folder = QDir(root()).filePath(QStringLiteral("vessel-c"));
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral(".hidden")), QStringLiteral("x")));

    const QStringList names = ManagedFileStore::entriesIn(folder);
    QCOMPARE(names, QStringList({QStringLiteral(".hidden")}));
}

QTEST_MAIN(TestManagedFileStore)
#include "tst_ManagedFileStore.moc"
