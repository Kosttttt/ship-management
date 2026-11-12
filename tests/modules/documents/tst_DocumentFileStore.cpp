#include "DocumentTestSupport.h"

#include "core/VesselRepository.h"
#include "modules/documents/service/DocumentFileStore.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §4.2: where a document's files go, what they
// are called, and the rule the whole module rests on — a version is never
// overwritten.
//
// Entirely inside a QTemporaryDir. Nothing here can reach the real documents
// folder (CLAUDE.md §10.1).
class TestDocumentFileStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aFleetWideDocumentIsFiledUnderFleetAndItsCategory();
    void aVesselsDocumentIsFiledUnderItsImoNumberAndCategory();
    void aVesselWithNoImoNumberIsRefusedRatherThanFiledNowhere();

    void theFileIsNamedAfterTheReferenceAndTheSequence();
    void aDocumentWithoutAReferenceIsNamedAfterItsTitle();
    void theOriginalExtensionIsKept();
    void aNameThatWouldBreakAFilesystemIsMadeSafe();

    void storingPutsTheFileWhereTheNameSays();
    void asecondVersionDoesNotOverwriteTheFirst();
    void storingWithNoDocumentsFolderSaysSo();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      root() const { return m_root.path(); }

    // A small real file to copy in, so "the file is there afterwards" means
    // what it says.
    QString sourceFile(const QString& name, const QByteArray& contents)
    {
        const QString path = m_source.filePath(name);
        return writeFile(path, contents) ? path : QString();
    }

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_root;
    QTemporaryDir m_source;
};

void TestDocumentFileStore::init()
{
    QVERIFY(m_root.isValid());
    QVERIFY(m_source.isValid());

    m_connectionName = QStringLiteral("docfiles_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    m_vesselId      = seedVessel(db, QStringLiteral("v1"), QStringLiteral("MV Alpha"),
                                 QStringLiteral("9074729"));
    QVERIFY(!m_vesselId.isEmpty());
}

void TestDocumentFileStore::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentFileStore::aFleetWideDocumentIsFiledUnderFleetAndItsCategory()
{
    QSqlDatabase      db = database();
    VesselRepository  vessels(db, officeContext());
    DocumentFileStore store(vessels);

    QString error;
    const QString folder = store.relativeFolderFor(
        sampleDocument(QString(), QStringLiteral("SMS-01"), QStringLiteral("Policy")), &error);

    QCOMPARE(folder, QStringLiteral("Fleet/SMS"));
    QVERIFY(error.isEmpty());
}

void TestDocumentFileStore::aVesselsDocumentIsFiledUnderItsImoNumberAndCategory()
{
    QSqlDatabase      db = database();
    VesselRepository  vessels(db, officeContext());
    DocumentFileStore store(vessels);

    QString error;
    const QString folder =
        store.relativeFolderFor(sampleDocument(m_vesselId, QStringLiteral("PLAN-1"),
                                               QStringLiteral("GA Plan"),
                                               DocumentCategory::PlanManual),
                                &error);

    // The IMO number, not the vessel's name or its UUID: a name can be
    // changed and a UUID means nothing to a person opening the folder.
    QCOMPARE(folder, QStringLiteral("9074729/PLAN_MANUAL"));
}

void TestDocumentFileStore::aVesselWithNoImoNumberIsRefusedRatherThanFiledNowhere()
{
    QSqlDatabase db = database();
    QSqlQuery    blank(db);
    QVERIFY(blank.exec(QStringLiteral("UPDATE vessel SET imo_number = '' WHERE id = 'v1'")));

    VesselRepository  vessels(db, officeContext());
    DocumentFileStore store(vessels);

    QString error;
    const QString folder = store.relativeFolderFor(
        sampleDocument(m_vesselId, QStringLiteral("PLAN-1"), QStringLiteral("GA Plan")), &error);

    // Filed under an empty name, every such vessel's documents would end up
    // in one folder together — which is worse than being told.
    QVERIFY(folder.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("IMO")), qPrintable(error));
}

void TestDocumentFileStore::theFileIsNamedAfterTheReferenceAndTheSequence()
{
    const Document document =
        sampleDocument(QString(), QStringLiteral("SMS-03"), QStringLiteral("Emergency"));

    // The name §4.2 gives as the example, and the one a person will see.
    QCOMPARE(DocumentFileStore::fileNameFor(document, 5, QStringLiteral("/tmp/whatever.pdf")),
             QStringLiteral("SMS-03__v5.pdf"));
}

void TestDocumentFileStore::aDocumentWithoutAReferenceIsNamedAfterItsTitle()
{
    const Document document =
        sampleDocument(QString(), QString(), QStringLiteral("Garbage Management Plan"));

    QCOMPARE(DocumentFileStore::fileNameFor(document, 1, QStringLiteral("x.docx")),
             QStringLiteral("Garbage Management Plan__v1.docx"));
}

void TestDocumentFileStore::theOriginalExtensionIsKept()
{
    const Document document =
        sampleDocument(QString(), QStringLiteral("REP-1"), QStringLiteral("Report"));

    // The computer's normal viewer is what opens these (§1), and it decides
    // by extension.
    QCOMPARE(DocumentFileStore::fileNameFor(document, 2, QStringLiteral("scan.tar.gz")),
             QStringLiteral("REP-1__v2.tar.gz"));

    // A source file with no extension keeps none rather than gaining one.
    QCOMPARE(DocumentFileStore::fileNameFor(document, 2, QStringLiteral("scan")),
             QStringLiteral("REP-1__v2"));
}

void TestDocumentFileStore::aNameThatWouldBreakAFilesystemIsMadeSafe()
{
    const Document document = sampleDocument(QString(), QStringLiteral("SMS/03: Rev?"),
                                             QStringLiteral("Emergency"));

    // These files are copied between computers (§4.2), so a name that is
    // legal on one and refused on another is worse than a plain one
    // everywhere. Windows refuses \ / : * ? " < > | outright.
    const QString name = DocumentFileStore::fileNameFor(document, 1, QStringLiteral("a.pdf"));
    for (const QChar character : QStringLiteral("\\/:*?\"<>|")) {
        QVERIFY2(!name.contains(character), qPrintable(name));
    }
    QCOMPARE(name, QStringLiteral("SMS_03_ Rev___v1.pdf"));
}

void TestDocumentFileStore::storingPutsTheFileWhereTheNameSays()
{
    QSqlDatabase      db = database();
    VesselRepository  vessels(db, officeContext());
    DocumentFileStore store(vessels);

    const QString source = sourceFile(QStringLiteral("original.pdf"), QByteArrayLiteral("one"));
    QVERIFY(!source.isEmpty());

    const Document document =
        sampleDocument(m_vesselId, QStringLiteral("REP-1"), QStringLiteral("PSC Inspection"),
                       DocumentCategory::Report);

    QString relative;
    QString error;
    QVERIFY2(store.store(root(), document, 1, source, &relative, &error), qPrintable(error));

    QCOMPARE(relative, QStringLiteral("9074729/REPORT/REP-1__v1.pdf"));
    QVERIFY(QFile::exists(QDir(root()).filePath(relative)));

    // And it resolves back to that same file, which is how it is opened.
    QCOMPARE(QFileInfo(DocumentFileStore::absolutePathFor(root(), relative)).canonicalFilePath(),
             QFileInfo(QDir(root()).filePath(relative)).canonicalFilePath());
}

void TestDocumentFileStore::asecondVersionDoesNotOverwriteTheFirst()
{
    QSqlDatabase      db = database();
    VesselRepository  vessels(db, officeContext());
    DocumentFileStore store(vessels);

    const Document document =
        sampleDocument(QString(), QStringLiteral("SMS-01"), QStringLiteral("Policy"));

    QString first;
    QString second;
    QString error;
    QVERIFY(store.store(root(), document, 1,
                        sourceFile(QStringLiteral("v1.pdf"), QByteArrayLiteral("first")), &first,
                        &error));
    QVERIFY(store.store(root(), document, 2,
                        sourceFile(QStringLiteral("v2.pdf"), QByteArrayLiteral("second")),
                        &second, &error));

    // The rule the archive rests on: two files, both still there, each with
    // its own contents. An overwrite here would quietly destroy the superseded
    // revision of an SMS manual.
    QVERIFY(first != second);
    QVERIFY(QFile::exists(QDir(root()).filePath(first)));
    QVERIFY(QFile::exists(QDir(root()).filePath(second)));

    QFile firstFile(QDir(root()).filePath(first));
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    QCOMPARE(firstFile.readAll(), QByteArrayLiteral("first"));
}

void TestDocumentFileStore::storingWithNoDocumentsFolderSaysSo()
{
    QSqlDatabase      db = database();
    VesselRepository  vessels(db, officeContext());
    DocumentFileStore store(vessels);

    QString relative;
    QString error;
    QVERIFY(!store.store(QString(),
                         sampleDocument(QString(), QStringLiteral("SMS-01"),
                                        QStringLiteral("Policy")),
                         1, QStringLiteral("whatever.pdf"), &relative, &error));

    // §4.4: it says so and points at Settings, rather than failing later with
    // something about a path.
    QVERIFY2(error.contains(QStringLiteral("Settings")), qPrintable(error));
}

QTEST_MAIN(TestDocumentFileStore)
#include "tst_DocumentFileStore.moc"
