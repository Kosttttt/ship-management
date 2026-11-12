#include "DocumentTestSupport.h"

#include "modules/documents/data/DocumentRepository.h"

#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §4.3 and §4.6: who may see and change what, and
// what a version history does.
//
// The scope rules are the ones that matter most here. A screen that forgot
// them would show one ship another ship's paperwork or let a vessel edit the
// company's SMS, which is why they live in the repository and are tested
// document-management-spec.md §4.6, the version half: what a history does,
// and what a soft delete leaves behind.
//
// Split from tst_DocumentScope along the spec's own two bullets — who may see
// and change what is a separate question from what a version is, and the one
// file holding both was over the ~300-line guideline (CLAUDE.md §9).
class TestDocumentVersions : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theFirstVersionIsCurrent();
    void aNewVersionBecomesCurrentAndTheOldOneIsSuperseded();
    void versionNumbersAreNeverReused();
    void aDocumentWithoutATitleIsRefused();
    void aDocumentWithoutAFileIsRefused();

    void deletingHidesTheDocumentAndItsVersions();
    void deletingLeavesTheRowsInTheDatabase();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
};

void TestDocumentVersions::init()
{
    m_connectionName = QStringLiteral("docversions_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestDocumentVersions::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentVersions::theFirstVersionIsCurrent()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString id;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &id));

    std::optional<DocumentVersion> current;
    QVERIFY(office.currentVersion(id, &current));
    QVERIFY(current.has_value());
    QCOMPARE(current->sequence, 1);
    // No label was given, so the sequence is what the list shows.
    QCOMPARE(current->displayLabel(), QStringLiteral("v1"));
}

void TestDocumentVersions::aNewVersionBecomesCurrentAndTheOldOneIsSuperseded()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString id;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &id));

    DocumentVersion second = sampleVersion(QStringLiteral("Fleet/SMS/b.pdf"));
    second.revisionLabel   = QStringLiteral("Rev 5");
    QVERIFY(office.addVersion(id, second));

    std::optional<DocumentVersion> current;
    QVERIFY(office.currentVersion(id, &current));
    QCOMPARE(current->sequence, 2);
    QCOMPARE(current->displayLabel(), QStringLiteral("Rev 5"));

    // The first one is still there and still points at its own file: nothing
    // was overwritten, which is what the archive means (§4.2).
    QList<DocumentVersion> versions;
    QVERIFY(office.listVersions(id, &versions));
    QCOMPARE(versions.size(), 2);
    QCOMPARE(versions.at(0).sequence, 2); // newest first
    QCOMPARE(versions.at(1).sequence, 1);
    QVERIFY(versions.at(0).filePath != versions.at(1).filePath);
}

void TestDocumentVersions::versionNumbersAreNeverReused()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString id;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &id));
    QVERIFY(office.addVersion(id, sampleVersion(QStringLiteral("Fleet/SMS/b.pdf"))));

    // Deleting the document retires its versions; a document created again
    // would be a different document. What matters here is the rule itself:
    // the next sequence counts past deleted rows, so a file named after a
    // sequence can never collide with an archived one.
    QSqlQuery retire(db);
    QVERIFY(retire.exec(QStringLiteral("UPDATE document_version SET is_deleted = 1"
                                       " WHERE sequence = 2")));

    QVERIFY(office.addVersion(id, sampleVersion(QStringLiteral("Fleet/SMS/c.pdf"))));

    std::optional<DocumentVersion> current;
    QVERIFY(office.currentVersion(id, &current));
    QCOMPARE(current->sequence, 3);
}

void TestDocumentVersions::aDocumentWithoutATitleIsRefused()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QVERIFY(!office.create(sampleDocument(QString(), QStringLiteral("SMS-01"), QString()),
                           sampleVersion(QStringLiteral("Fleet/SMS/a.pdf"))));
    QVERIFY2(office.errorString().contains(QStringLiteral("title")),
             qPrintable(office.errorString()));
}

void TestDocumentVersions::aDocumentWithoutAFileIsRefused()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    // A document with no version has nothing to open, so the two are created
    // together or not at all.
    QVERIFY(!office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                          QStringLiteral("Company Policy")),
                           DocumentVersion()));
    QCOMPARE(rowCountOf(db, QStringLiteral("document")), 0);
}

void TestDocumentVersions::deletingHidesTheDocumentAndItsVersions()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString id;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &id));
    QVERIFY(office.addVersion(id, sampleVersion(QStringLiteral("Fleet/SMS/b.pdf"))));

    QVERIFY(office.softDelete(id));

    QList<DocumentRepository::ListRow> rows;
    QVERIFY(office.list(QString(), &rows));
    QVERIFY(rows.isEmpty());

    QList<DocumentVersion> versions;
    QVERIFY(office.listVersions(id, &versions));
    QVERIFY2(versions.isEmpty(), "a deleted document's history must not reappear");
}

void TestDocumentVersions::deletingLeavesTheRowsInTheDatabase()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString id;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &id));
    QVERIFY(office.addVersion(id, sampleVersion(QStringLiteral("Fleet/SMS/b.pdf"))));

    QVERIFY(office.softDelete(id));

    // Soft delete (CLAUDE.md §6.4): the rows are still there, flagged, so the
    // deletion can replicate to the other side as a change rather than as an
    // absence — and so nothing is actually destroyed.
    QCOMPARE(rowCountOf(db, QStringLiteral("document")), 1);
    QCOMPARE(rowCountOf(db, QStringLiteral("document_version")), 2);
}

// QTEST_MAIN, not QTEST_APPLESS_MAIN: no widgets here, but opening a SQLite
// connection needs an application object.
QTEST_MAIN(TestDocumentVersions)
#include "tst_DocumentVersions.moc"
