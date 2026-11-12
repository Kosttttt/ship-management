#include "DocumentTestSupport.h"

#include "modules/documents/data/DocumentRepository.h"

#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §4.3 and §4.6: who may see and change what.
//
// What a version history does is tst_DocumentVersions, split along the
// spec's own two bullets once the one file holding both went over the
// ~300-line guideline (CLAUDE.md §9).
//
// The scope rules are the ones that matter most here. A screen that forgot
// them would show one ship another ship's paperwork or let a vessel edit the
// company's SMS, which is why they live in the repository and are tested
// without a widget in sight (CLAUDE.md §3).
class TestDocumentScope : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Scope
    void officeSeesFleetWideAndTheChosenVessel();
    void officeWithNoVesselChosenSeesOnlyFleetWide();
    void aVesselSeesFleetWideAndItsOwn();
    void aVesselCannotSeeAnotherVesselsDocumentsByAnyRoute();

    // §4.3's write rule
    void aVesselCannotAddAFleetWideDocument();
    void aVesselCannotEditAFleetWideDocument();
    void aVesselCannotAddAVersionToAFleetWideDocument();
    void aVesselCannotDeleteAFleetWideDocument();
    void aVesselCanManageItsOwnDocuments();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
    QString m_ownVesselId;
    QString m_otherVesselId;
};

void TestDocumentScope::init()
{
    m_connectionName = QStringLiteral("docrepo_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    m_ownVesselId   = seedVessel(db, QStringLiteral("v1"), QStringLiteral("MV Alpha"),
                                 QStringLiteral("9074729"));
    m_otherVesselId = seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Beta"),
                                 QStringLiteral("9074730"));
    QVERIFY(!m_ownVesselId.isEmpty());
    QVERIFY(!m_otherVesselId.isEmpty());
}

void TestDocumentScope::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestDocumentScope::officeSeesFleetWideAndTheChosenVessel()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/SMS-01__v1.pdf"))));
    QVERIFY(office.create(sampleDocument(m_ownVesselId, QStringLiteral("PLAN-1"),
                                         QStringLiteral("GA Plan"),
                                         DocumentCategory::PlanManual),
                          sampleVersion(QStringLiteral("9074729/PLAN_MANUAL/PLAN-1__v1.pdf"))));
    QVERIFY(office.create(sampleDocument(m_otherVesselId, QStringLiteral("PLAN-2"),
                                         QStringLiteral("Beta GA Plan"),
                                         DocumentCategory::PlanManual),
                          sampleVersion(QStringLiteral("9074730/PLAN_MANUAL/PLAN-2__v1.pdf"))));

    QList<DocumentRepository::ListRow> rows;
    QVERIFY2(office.list(m_ownVesselId, &rows), qPrintable(office.errorString()));

    // The fleet's document and this vessel's — not the other ship's, which
    // belongs to whichever vessel is selected in the toolbar instead.
    QCOMPARE(rows.size(), 2);
    QStringList references;
    for (const DocumentRepository::ListRow& row : rows) {
        references.append(row.document.reference);
    }
    QVERIFY(references.contains(QStringLiteral("SMS-01")));
    QVERIFY(references.contains(QStringLiteral("PLAN-1")));
    QVERIFY(!references.contains(QStringLiteral("PLAN-2")));
}

void TestDocumentScope::officeWithNoVesselChosenSeesOnlyFleetWide()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf"))));
    QVERIFY(office.create(sampleDocument(m_ownVesselId, QStringLiteral("PLAN-1"),
                                         QStringLiteral("GA Plan")),
                          sampleVersion(QStringLiteral("9074729/SMS/b.pdf"))));

    QList<DocumentRepository::ListRow> rows;
    QVERIFY(office.list(QString(), &rows));

    // What the screen shows before a vessel is picked in the toolbar: the
    // company's documents, which are not about any one ship.
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().document.reference, QStringLiteral("SMS-01"));
}

void TestDocumentScope::aVesselSeesFleetWideAndItsOwn()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf"))));
    QVERIFY(office.create(sampleDocument(m_ownVesselId, QStringLiteral("PLAN-1"),
                                         QStringLiteral("GA Plan")),
                          sampleVersion(QStringLiteral("9074729/SMS/b.pdf"))));

    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));

    QList<DocumentRepository::ListRow> rows;
    QVERIFY(onBoard.list(QString(), &rows));

    // Asked for nothing in particular, a ship still gets its own: the scope
    // comes from the installation, not from the argument.
    QCOMPARE(rows.size(), 2);
}

void TestDocumentScope::aVesselCannotSeeAnotherVesselsDocumentsByAnyRoute()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString otherId;
    QVERIFY(office.create(sampleDocument(m_otherVesselId, QStringLiteral("PLAN-2"),
                                         QStringLiteral("Beta GA Plan")),
                          sampleVersion(QStringLiteral("9074730/SMS/b.pdf")), &otherId));

    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));

    // Not in the list, even when asked for by name...
    QList<DocumentRepository::ListRow> rows;
    QVERIFY(onBoard.list(m_otherVesselId, &rows));
    QVERIFY(rows.isEmpty());

    // ...nor reachable by its id, which is the route a screen would take.
    std::optional<Document> found;
    QVERIFY(onBoard.findById(otherId, &found));
    QVERIFY2(!found.has_value(), "another vessel's document must not be readable by id");

    // ...nor is its history.
    QList<DocumentVersion> versions;
    QVERIFY(onBoard.listVersions(otherId, &versions));
    QVERIFY(versions.isEmpty());
}

void TestDocumentScope::aVesselCannotAddAFleetWideDocument()
{
    QSqlDatabase       db = database();
    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));

    // §4.3: fleet-wide documents arrive from the office — step 4 of this
    // module is what will bring them — and are read-only on a ship.
    QVERIFY(!onBoard.create(sampleDocument(QString(), QStringLiteral("SMS-99"),
                                           QStringLiteral("Invented On Board")),
                            sampleVersion(QStringLiteral("Fleet/SMS/x.pdf"))));
    QVERIFY2(onBoard.errorString().contains(QStringLiteral("office")),
             qPrintable(onBoard.errorString()));

    QCOMPARE(rowCountOf(db, QStringLiteral("document")), 0);
}

void TestDocumentScope::aVesselCannotEditAFleetWideDocument()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString fleetId;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &fleetId));

    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));

    std::optional<Document> fleet;
    QVERIFY(onBoard.findById(fleetId, &fleet));
    QVERIFY2(fleet.has_value(), "a vessel may read fleet-wide documents");

    Document edited = *fleet;
    edited.title    = QStringLiteral("Rewritten On Board");
    QVERIFY(!onBoard.updateDetails(edited));

    // And the stored row is untouched, not merely the call refused.
    std::optional<Document> reread;
    QVERIFY(office.findById(fleetId, &reread));
    QCOMPARE(reread->title, QStringLiteral("Company Policy"));
}

void TestDocumentScope::aVesselCannotAddAVersionToAFleetWideDocument()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString fleetId;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &fleetId));

    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));

    // The one that would matter most: a ship issuing its own revision of the
    // company's SMS is exactly what document control exists to prevent.
    QVERIFY(!onBoard.addVersion(fleetId, sampleVersion(QStringLiteral("Fleet/SMS/b.pdf"))));

    QList<DocumentVersion> versions;
    QVERIFY(office.listVersions(fleetId, &versions));
    QCOMPARE(versions.size(), 1);
}

void TestDocumentScope::aVesselCannotDeleteAFleetWideDocument()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    QString fleetId;
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf")), &fleetId));

    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));
    QVERIFY(!onBoard.softDelete(fleetId));

    QList<DocumentRepository::ListRow> rows;
    QVERIFY(office.list(QString(), &rows));
    QCOMPARE(rows.size(), 1);
}

void TestDocumentScope::aVesselCanManageItsOwnDocuments()
{
    QSqlDatabase       db = database();
    DocumentRepository onBoard(db, vesselContext(m_ownVesselId, QStringLiteral("9074729")));

    // The other half of §4.3, and the half that makes the module usable at
    // sea: its own paperwork is entirely the ship's to manage.
    QString id;
    QVERIFY2(onBoard.create(sampleDocument(m_ownVesselId, QStringLiteral("REP-1"),
                                           QStringLiteral("PSC Inspection"),
                                           DocumentCategory::Report),
                            sampleVersion(QStringLiteral("9074729/REPORT/REP-1__v1.pdf")), &id),
             qPrintable(onBoard.errorString()));

    QVERIFY(onBoard.addVersion(id, sampleVersion(QStringLiteral("9074729/REPORT/REP-1__v2.pdf"))));

    std::optional<Document> document;
    QVERIFY(onBoard.findById(id, &document));
    Document edited = *document;
    edited.title    = QStringLiteral("PSC Inspection, Rotterdam");
    QVERIFY(onBoard.updateDetails(edited));

    QVERIFY(onBoard.softDelete(id));
}

// QTEST_MAIN, not QTEST_APPLESS_MAIN: no widgets here, but opening a SQLite
// connection needs an application object.
QTEST_MAIN(TestDocumentScope)
#include "tst_DocumentScope.moc"
