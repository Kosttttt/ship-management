#include "core/Database.h"
#include "core/ManagedFileStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

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

    void resolveAbsoluteJoinsTheRoot();
    void anEmptyRelativePathResolvesToNothing();
    void theSameRelativePathResolvesUnderADifferentRoot();

private:
    QString root() const { return m_root.path(); }

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
    // The managed data folder for the duration of one test.
    QTemporaryDir m_root;
};

void TestManagedFileStore::init()
{
    QVERIFY(m_source.isValid());
    QVERIFY(m_root.isValid());
    ManagedFileStore::setRootPathOverride(m_root.path());
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
    QVERIFY2(ManagedFileStore::copyInto(source, QStringLiteral("attachments/c1"),
                                        QStringLiteral("15D_Load_Line_2027-06-01.pdf"), &relative,
                                        &error),
             qPrintable(error));

    // Relative, with forward slashes: this is the string the database stores
    // (CLAUDE.md §6.9).
    QCOMPARE(relative, QStringLiteral("attachments/c1/15D_Load_Line_2027-06-01.pdf"));
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
    QVERIFY2(ManagedFileStore::copyInto(sourceDir, QStringLiteral("attachments/c1"),
                                        QStringLiteral("15D_Load_Line_2027-06-01"), &relative,
                                        &error),
             qPrintable(error));

    QCOMPARE(relative, QStringLiteral("attachments/c1/15D_Load_Line_2027-06-01"));

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
    QVERIFY2(ManagedFileStore::copyInto(source, QStringLiteral("attachments/c1/extensions"),
                                        QStringLiteral("15D_ShortTerm_2027-03-01.pdf"), &first,
                                        &error),
             qPrintable(error));

    QString second;
    QVERIFY2(ManagedFileStore::copyInto(source, QStringLiteral("attachments/c1/extensions"),
                                        QStringLiteral("15D_ShortTerm_2027-03-01.pdf"), &second,
                                        &error),
             qPrintable(error));

    QCOMPARE(first, QStringLiteral("attachments/c1/extensions/15D_ShortTerm_2027-03-01.pdf"));
    // The suffix goes before the extension, or the operating system stops
    // recognising the file.
    QCOMPARE(second, QStringLiteral("attachments/c1/extensions/15D_ShortTerm_2027-03-01_2.pdf"));

    QVERIFY(QFileInfo::exists(QDir(root()).filePath(first)));
    QVERIFY(QFileInfo::exists(QDir(root()).filePath(second)));
}

void TestManagedFileStore::aMissingSourceFailsAndLeavesNothingBehind()
{
    const QString missing = QDir(m_source.path()).filePath(QStringLiteral("not-there.pdf"));

    QString relative;
    QString error;
    QVERIFY(!ManagedFileStore::copyInto(missing, QStringLiteral("attachments/c1"),
                                        QStringLiteral("name.pdf"), &relative, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(relative.isEmpty());

    QVERIFY2(!QFileInfo::exists(
                 QDir(root()).filePath(QStringLiteral("attachments/c1/name.pdf"))),
             "a failed copy must leave nothing for a row to point at");
}

void TestManagedFileStore::archivingAFileMovesItAndStampsTheName()
{
    const QString current = QDir(root()).filePath(QStringLiteral("attachments/c1/current.pdf"));
    QVERIFY(QDir().mkpath(QFileInfo(current).absolutePath()));
    QVERIFY(writeFile(current, QStringLiteral("the old document")));

    QString error;
    QVERIFY2(ManagedFileStore::moveToArchive(QStringLiteral("attachments/c1/current.pdf"),
                                             QStringLiteral("attachments/c1/archive"), &error),
             qPrintable(error));

    // Moved, not copied: the current folder is meant to hold exactly what is
    // current (§4).
    QVERIFY(!QFileInfo::exists(current));

    const QDir archive(QDir(root()).filePath(QStringLiteral("attachments/c1/archive")));
    const QStringList archived = archive.entryList(QDir::Files);
    QCOMPARE(archived.size(), 1);

    // §4: the suffix exists only to make the name unique, and is not meant to
    // be read — but the original name has to survive in front of it.
    QVERIFY2(archived.first().startsWith(QStringLiteral("current.pdf__archived-")),
             qPrintable(archived.first()));

    // §9 case 8: nothing is ever actually deleted.
    QCOMPARE(readFile(archive.filePath(archived.first())), QStringLiteral("the old document"));
}

void TestManagedFileStore::archivingAFolderMovesTheWholeThing()
{
    const QString folder = QDir(root()).filePath(QStringLiteral("attachments/c1/pack"));
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("inside.txt")),
                      QStringLiteral("still here")));

    QString error;
    QVERIFY2(ManagedFileStore::moveToArchive(QStringLiteral("attachments/c1/pack"),
                                             QStringLiteral("attachments/c1/archive"), &error),
             qPrintable(error));

    QVERIFY(!QFileInfo::exists(folder));

    const QDir        archive(QDir(root()).filePath(QStringLiteral("attachments/c1/archive")));
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
    QVERIFY(!ManagedFileStore::moveToArchive(QStringLiteral("attachments/c1/ghost.pdf"),
                                             QStringLiteral("attachments/c1/archive"), &error));
    QVERIFY(!error.isEmpty());

    // An empty path is a caller mistake, not "nothing to do": the caller skips
    // archiving itself when there is no current attachment.
    QVERIFY(!ManagedFileStore::moveToArchive(QString(), QStringLiteral("attachments/c1/archive"),
                                             &error));
}

void TestManagedFileStore::resolveAbsoluteJoinsTheRoot()
{
    const QString relative = QStringLiteral("attachments/c1/15D_Load_Line_2027-06-01.pdf");
    QCOMPARE(ManagedFileStore::resolveAbsolute(relative), QDir(root()).filePath(relative));
}

void TestManagedFileStore::anEmptyRelativePathResolvesToNothing()
{
    // "There is no attachment" must never resolve to the managed folder
    // itself, which something would then helpfully open.
    QCOMPARE(ManagedFileStore::resolveAbsolute(QString()), QString());
}

// §9 case 9: the concrete proof that CLAUDE.md §6.9 holds for this feature.
// The same stored string points at the right file on a second machine, because
// what is stored is relative and only the root differs.
void TestManagedFileStore::theSameRelativePathResolvesUnderADifferentRoot()
{
    const QString relative = QStringLiteral("attachments/c1/15D_Load_Line_2027-06-01.pdf");

    QVERIFY(QDir().mkpath(QFileInfo(QDir(root()).filePath(relative)).absolutePath()));
    QVERIFY(writeFile(QDir(root()).filePath(relative), QStringLiteral("the ship copy")));

    const QString onTheShip = ManagedFileStore::resolveAbsolute(relative);
    QVERIFY(QFileInfo::exists(onTheShip));

    // A second folder standing in for the office machine, with the same
    // managed folder copied into it.
    QTemporaryDir office;
    QVERIFY(office.isValid());
    QVERIFY(QDir().mkpath(QFileInfo(QDir(office.path()).filePath(relative)).absolutePath()));
    QVERIFY(writeFile(QDir(office.path()).filePath(relative), QStringLiteral("the office copy")));

    ManagedFileStore::setRootPathOverride(office.path());

    const QString inTheOffice = ManagedFileStore::resolveAbsolute(relative);
    QVERIFY(inTheOffice != onTheShip);
    QVERIFY(QFileInfo::exists(inTheOffice));
    QCOMPARE(readFile(inTheOffice), QStringLiteral("the office copy"));
}

QTEST_MAIN(TestManagedFileStore)
#include "tst_ManagedFileStore.moc"
