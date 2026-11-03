#include "CertificateTestSupport.h"
#include "CsvImportTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateCsvExporter.h"
#include "modules/certificates/service/CertificateCsvImporter.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace CertificateTestSupport;
using namespace CsvImportTestSupport;

// certificate-import-export-spec §5 and §7: matching, scope, and every
// file-level refusal (§9.1 to §9.7, §9.19).
class TestCertificateCsvImport : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §9.2
    void aFileWithNoIdColumnIsAllNewRows();
    void aBlankIdIsANewRow();
    void importingNewRowsCreatesThemAgainstTheSelectedVessel();

    // §9.1
    void aRoundTripIsAllMatches();
    void overwritingAMatchChangesNothingButTheRevisionAndStamp();
    void aSkippedMatchIsNotWrittenAtAll();
    void skipIsTheDefaultForAMatch();

    // §9.3
    void anIdBelongingToAnotherVesselIsNotAMatch();
    void thatRowIsImportedAsNewAgainstTheSelectedVessel();
    void theOtherVesselsCertificateIsLeftAlone();

    // §9.4
    void anIdOfASoftDeletedCertificateReadsAsNewPreviouslyDeleted();
    void theResurrectedCertificateGetsItsOwnNewIdAndLeavesTheOldRowDeleted();

    // §9.5
    void aWrongVesselImoRefusesTheWholeFile();
    void nothingIsWrittenWhenTheImoDoesNotMatch();
    void aFileImoIsAMismatchWhenTheVesselHasNoImoRecorded();
    void aBlankVesselImoIsNotChecked();

    // §9.6
    void theSameIdTwiceIsAFileLevelRefusal();

    // §9.7
    void aMissingRequiredHeaderRefusesBeforeAnyRowIsParsed();

    // §9.14, at the import rather than the parsing level
    void anInvalidRowIsPreviewedButBlocksTheWholeImport();
    void aValidRowBesideAnInvalidOneIsNotWrittenEither();

    // §9.19
    void vesselModeNeverImportsAgainstAnotherVessel();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      pathFor(const QString& name) { return m_folder.filePath(name); }

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_folder;
};

void TestCertificateCsvImport::init()
{
    m_connectionName = QStringLiteral("csvimport_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_folder.isValid());

    m_vesselId      = QStringLiteral("v1");
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, m_vesselId, QStringLiteral("MV Aurora"),
                        QStringLiteral("9074729"))
                 .isEmpty());
}

void TestCertificateCsvImport::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateCsvImport::aFileWithNoIdColumnIsAllNewRows()
{
    // A hand-built file: three columns, no Id at all.
    const QStringList header = {QStringLiteral("Name"), QStringLiteral("Category"),
                                QStringLiteral("Issue Date")};
    const QString     path =
        writeCsv(pathFor(QStringLiteral("hand.csv")),
                 {{QStringLiteral("Load Line"), QStringLiteral("CLASS"),
                   QStringLiteral("2026-01-01")},
                  {QStringLiteral("Safety Radio"), QStringLiteral("STATUTORY"),
                   QStringLiteral("2026-02-04")}},
                 header);
    QVERIFY(!path.isEmpty());

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 2);
    QCOMPARE(preview.countOf(ImportRowOutcome::New), 2);
    QVERIFY(!preview.hasInvalidRows());
}

void TestCertificateCsvImport::aBlankIdIsANewRow()
{
    const QString path = writeCsv(pathFor(QStringLiteral("blank.csv")),
                                  {rowWith(validValues(QStringLiteral("Safety Radio")))});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 1);
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::New);
}

void TestCertificateCsvImport::importingNewRowsCreatesThemAgainstTheSelectedVessel()
{
    const QString path = writeCsv(pathFor(QStringLiteral("new.csv")),
                                  {rowWith(validValues(QStringLiteral("Safety Radio")))});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));

    int created = 0;
    int updated = 0;
    QVERIFY2(importer.runImport(m_vesselId, preview, &created, &updated),
             qPrintable(importer.errorString()));
    QCOMPARE(created, 1);
    QCOMPARE(updated, 0);

    QList<Certificate> stored;
    QVERIFY(certificates.list(m_vesselId, &stored));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().name, QStringLiteral("Safety Radio"));
    QCOMPARE(stored.first().vesselId, m_vesselId);
    QCOMPARE(stored.first().issueDate, QDate(2026, 2, 4));
    QCOMPARE(stored.first().expiryDate, QDate(2031, 2, 4));
}

void TestCertificateCsvImport::aRoundTripIsAllMatches()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Load Line"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    const QString path = pathFor(QStringLiteral("round.csv"));
    QVERIFY2(exporter.exportToFile(m_vesselId, path), qPrintable(exporter.errorString()));

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 2);
    QCOMPARE(preview.countOf(ImportRowOutcome::Match), 2);
    QCOMPARE(preview.countOf(ImportRowOutcome::New), 0);
}

void TestCertificateCsvImport::overwritingAMatchChangesNothingButTheRevisionAndStamp()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    std::optional<Certificate> before;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &before));
    QVERIFY(before.has_value());

    const QString path = pathFor(QStringLiteral("round.csv"));
    QVERIFY(exporter.exportToFile(m_vesselId, path));

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    for (CertificateImportRow& row : preview.rows) {
        row.overwrite = true;
    }

    int created = 0;
    int updated = 0;
    QVERIFY2(importer.runImport(m_vesselId, preview, &created, &updated),
             qPrintable(importer.errorString()));
    QCOMPARE(created, 0);
    QCOMPARE(updated, 1);

    std::optional<Certificate> after;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &after));
    QVERIFY(after.has_value());

    // Every value the file carried comes back identical...
    QCOMPARE(after->id, before->id);
    QCOMPARE(after->name, before->name);
    QCOMPARE(after->category, before->category);
    QCOMPARE(after->issueDate, before->issueDate);
    QCOMPARE(after->expiryDate, before->expiryDate);
    QCOMPARE(after->certificateNumber, before->certificateNumber);

    // ...and revision still moves, because this was an ordinary update()
    // whether or not the values differed (§9.1).
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT revision, updated_by FROM certificate"
                                      " WHERE id = 'c1'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QCOMPARE(query.value(1).toString(), QStringLiteral("SYSTEM"));
}

void TestCertificateCsvImport::aSkippedMatchIsNotWrittenAtAll()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    const QString path = pathFor(QStringLiteral("round.csv"));
    QVERIFY(exporter.exportToFile(m_vesselId, path));

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));

    int created = 0;
    int updated = 0;
    QVERIFY(importer.runImport(m_vesselId, preview, &created, &updated));
    QCOMPARE(created, 0);
    QCOMPARE(updated, 0);

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT revision FROM certificate WHERE id = 'c1'")));
    QVERIFY(query.next());
    QVERIFY2(query.value(0).toInt() == 1, "a skipped row must not even bump the revision");
}

void TestCertificateCsvImport::skipIsTheDefaultForAMatch()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    const QString path = pathFor(QStringLiteral("round.csv"));
    QVERIFY(exporter.exportToFile(m_vesselId, path));

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QCOMPARE(preview.rows.size(), 1);
    QVERIFY2(!preview.rows.first().overwrite,
             "§5.5: a match defaults to skip, so an unattended import overwrites nothing");
}

void TestCertificateCsvImport::anIdBelongingToAnotherVesselIsNotAMatch()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9074730"));
    seedCertificate(db, QStringLiteral("theirs"), QStringLiteral("v2"),
                    QStringLiteral("Their Certificate"));

    QHash<QString, QString> values = validValues(QStringLiteral("My Certificate"));
    values.insert(QStringLiteral("Id"), QStringLiteral("theirs"));
    const QString path = writeCsv(pathFor(QStringLiteral("cross.csv")), {rowWith(values)});

    QSqlDatabase           dbRef = database();
    CertificateRepository  certificates(dbRef, officeContext());
    EndorsementRepository  endorsements(dbRef, officeContext());
    VesselRepository       vessels(dbRef, officeContext());
    CertificateCsvImporter importer(dbRef, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 1);
    // §5.1: "not found", not "another vessel's certificate found". findById()
    // applies no vessel filter in OFFICE mode, which is exactly why matching
    // reads the scoped list instead.
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::New);
    QVERIFY(preview.rows.first().matchedId.isEmpty());
}

void TestCertificateCsvImport::thatRowIsImportedAsNewAgainstTheSelectedVessel()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9074730"));
    seedCertificate(db, QStringLiteral("theirs"), QStringLiteral("v2"),
                    QStringLiteral("Their Certificate"));

    QHash<QString, QString> values = validValues(QStringLiteral("My Certificate"));
    values.insert(QStringLiteral("Id"), QStringLiteral("theirs"));
    const QString path = writeCsv(pathFor(QStringLiteral("cross.csv")), {rowWith(values)});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QVERIFY2(importer.runImport(m_vesselId, preview), qPrintable(importer.errorString()));

    QList<Certificate> mine;
    QVERIFY(certificates.list(m_vesselId, &mine));
    QCOMPARE(mine.size(), 1);
    QCOMPARE(mine.first().name, QStringLiteral("My Certificate"));
    QVERIFY2(mine.first().id != QStringLiteral("theirs"),
             "a fresh UUID, never the id the file borrowed from another ship");
}

void TestCertificateCsvImport::theOtherVesselsCertificateIsLeftAlone()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9074730"));
    seedCertificate(db, QStringLiteral("theirs"), QStringLiteral("v2"),
                    QStringLiteral("Their Certificate"));

    QHash<QString, QString> values = validValues(QStringLiteral("My Certificate"));
    values.insert(QStringLiteral("Id"), QStringLiteral("theirs"));
    const QString path = writeCsv(pathFor(QStringLiteral("cross.csv")), {rowWith(values)});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QVERIFY(importer.runImport(m_vesselId, preview));

    // The whole point of §5.1's fix: the other ship's certificate still
    // belongs to the other ship, with its own name and revision.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT vessel_id, name, revision FROM certificate"
                                      " WHERE id = 'theirs'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("v2"));
    QCOMPARE(query.value(1).toString(), QStringLiteral("Their Certificate"));
    QCOMPARE(query.value(2).toInt(), 1);
}

void TestCertificateCsvImport::anIdOfASoftDeletedCertificateReadsAsNewPreviouslyDeleted()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("gone"), m_vesselId, QStringLiteral("Old Certificate"),
                    true);

    QHash<QString, QString> values = validValues(QStringLiteral("Old Certificate"));
    values.insert(QStringLiteral("Id"), QStringLiteral("gone"));
    const QString path = writeCsv(pathFor(QStringLiteral("deleted.csv")), {rowWith(values)});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 1);
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::NewPreviouslyDeleted);
}

void TestCertificateCsvImport::theResurrectedCertificateGetsItsOwnNewIdAndLeavesTheOldRowDeleted()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("gone"), m_vesselId, QStringLiteral("Old Certificate"),
                    true);

    QHash<QString, QString> values = validValues(QStringLiteral("Old Certificate"));
    values.insert(QStringLiteral("Id"), QStringLiteral("gone"));
    const QString path = writeCsv(pathFor(QStringLiteral("deleted.csv")), {rowWith(values)});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QVERIFY2(importer.runImport(m_vesselId, preview), qPrintable(importer.errorString()));

    QList<Certificate> live;
    QVERIFY(certificates.list(m_vesselId, &live));
    QCOMPARE(live.size(), 1);
    QVERIFY2(live.first().id != QStringLiteral("gone"),
             "create() mints its own UUID; the file's id cannot be reused");

    // The deleted row is untouched: soft delete still means soft delete.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT is_deleted FROM certificate WHERE id = 'gone'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestCertificateCsvImport::aWrongVesselImoRefusesTheWholeFile()
{
    QHash<QString, QString> good = validValues(QStringLiteral("Mine"));
    good.insert(QStringLiteral("Vessel IMO"), QStringLiteral("9074729"));
    QHash<QString, QString> wrong = validValues(QStringLiteral("Not Mine"));
    wrong.insert(QStringLiteral("Vessel IMO"), QStringLiteral("9074730"));

    const QString path = writeCsv(pathFor(QStringLiteral("imo.csv")),
                                  {rowWith(good), rowWith(wrong)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(!importer.buildPreview(m_vesselId, path, &preview),
             "a file carrying another ship's IMO number is the wrong file, not a bad row");
    QVERIFY(importer.errorString().contains(QStringLiteral("9074730")));
    QVERIFY(importer.errorString().contains(QStringLiteral("9074729")));
    QVERIFY2(preview.rows.isEmpty(), "a refusal previews nothing");
}

void TestCertificateCsvImport::nothingIsWrittenWhenTheImoDoesNotMatch()
{
    QHash<QString, QString> good = validValues(QStringLiteral("Mine"));
    good.insert(QStringLiteral("Vessel IMO"), QStringLiteral("9074729"));
    QHash<QString, QString> wrong = validValues(QStringLiteral("Not Mine"));
    wrong.insert(QStringLiteral("Vessel IMO"), QStringLiteral("9074730"));

    const QString path = writeCsv(pathFor(QStringLiteral("imo.csv")),
                                  {rowWith(good), rowWith(wrong)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(!importer.buildPreview(m_vesselId, path, &preview));

    // §9.5: including the valid row.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateCsvImport::aFileImoIsAMismatchWhenTheVesselHasNoImoRecorded()
{
    // §9.5's second half. Seeded directly, because the vessel repository
    // would never accept a blank IMO through its own validation.
    QSqlDatabase db = database();
    QSqlQuery    blank(db);
    QVERIFY(blank.exec(QStringLiteral("UPDATE vessel SET imo_number = '' WHERE id = 'v1'")));

    QHash<QString, QString> values = validValues(QStringLiteral("Mine"));
    values.insert(QStringLiteral("Vessel IMO"), QStringLiteral("9074729"));
    const QString path = writeCsv(pathFor(QStringLiteral("noimo.csv")), {rowWith(values)});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(!importer.buildPreview(m_vesselId, path, &preview),
             "a file that names an IMO the vessel does not have is refused, not waved through");
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateCsvImport::aBlankVesselImoIsNotChecked()
{
    // §7: a hand-built file reasonably carries no IMO at all.
    const QString path = writeCsv(pathFor(QStringLiteral("plain.csv")),
                                  {rowWith(validValues(QStringLiteral("Mine")))});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 1);
}

void TestCertificateCsvImport::theSameIdTwiceIsAFileLevelRefusal()
{
    QHash<QString, QString> first = validValues(QStringLiteral("One Version"));
    first.insert(QStringLiteral("Id"), QStringLiteral("c1"));
    QHash<QString, QString> second = validValues(QStringLiteral("Another Version"));
    second.insert(QStringLiteral("Id"), QStringLiteral("c1"));

    const QString path = writeCsv(pathFor(QStringLiteral("dupe.csv")),
                                  {rowWith(first), rowWith(second)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(!importer.buildPreview(m_vesselId, path, &preview),
             "neither first-one-wins nor last-one-wins is a defensible guess");
    QVERIFY(importer.errorString().contains(QStringLiteral("c1")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateCsvImport::aMissingRequiredHeaderRefusesBeforeAnyRowIsParsed()
{
    const QStringList header = {QStringLiteral("Name"), QStringLiteral("Issue Date")};
    const QString     path   = writeCsv(pathFor(QStringLiteral("noheader.csv")),
                                    {{QStringLiteral("Load Line"), QStringLiteral("2026-01-01")}},
                                    header);

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(!importer.buildPreview(m_vesselId, path, &preview));
    QVERIFY(importer.errorString().contains(QStringLiteral("Category")));
    QVERIFY2(preview.rows.isEmpty(), "not one Invalid row per line — the file itself is wrong");
}

void TestCertificateCsvImport::anInvalidRowIsPreviewedButBlocksTheWholeImport()
{
    QHash<QString, QString> bad = validValues(QStringLiteral("Bad Date"));
    bad.insert(QStringLiteral("Issue Date"), QStringLiteral("04/02/2026"));

    const QString path = writeCsv(pathFor(QStringLiteral("invalid.csv")),
                                  {rowWith(validValues(QStringLiteral("Good"))), rowWith(bad)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    // An invalid row is not a file-level refusal: the user has to see which
    // row to fix (§5.5).
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 2);
    QVERIFY(preview.hasInvalidRows());
    QCOMPARE(preview.countOf(ImportRowOutcome::Invalid), 1);
    QVERIFY(!preview.rows.at(1).reason.isEmpty());

    QVERIFY2(!importer.runImport(m_vesselId, preview),
             "the whole import is blocked while any row is Invalid");
}

void TestCertificateCsvImport::aValidRowBesideAnInvalidOneIsNotWrittenEither()
{
    QHash<QString, QString> bad = validValues(QStringLiteral("Bad Category"));
    bad.insert(QStringLiteral("Category"), QStringLiteral("NONSENSE"));

    const QString path = writeCsv(pathFor(QStringLiteral("invalid.csv")),
                                  {rowWith(validValues(QStringLiteral("Good"))), rowWith(bad)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QVERIFY(!importer.runImport(m_vesselId, preview));

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateCsvImport::vesselModeNeverImportsAgainstAnotherVessel()
{
    // §9.19: in VESSEL mode the scope is the installation's own vessel, and
    // the repository refuses a write aimed anywhere else — so even asking to
    // import against another vessel's id changes nothing.
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9074730"));

    const QString path = writeCsv(pathFor(QStringLiteral("vesselmode.csv")),
                                  {rowWith(validValues(QStringLiteral("Mine")))});

    const InstallationContext vesselInstallation = vesselContext(m_vesselId);
    CertificateRepository     certificates(db, vesselInstallation);
    EndorsementRepository     endorsements(db, vesselInstallation);
    VesselRepository          vessels(db, vesselInstallation);
    CertificateCsvImporter    importer(db, certificates, endorsements, vessels);

    // Its own vessel: normal.
    CertificateImportPreview mine;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &mine), qPrintable(importer.errorString()));
    QVERIFY2(importer.runImport(m_vesselId, mine), qPrintable(importer.errorString()));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 1);

    // Another vessel: the scoped read cannot even see it, and the write is
    // refused rather than quietly landing somewhere.
    CertificateImportPreview theirs;
    const bool previewed = importer.buildPreview(QStringLiteral("v2"), path, &theirs);
    if (previewed) {
        QVERIFY2(!importer.runImport(QStringLiteral("v2"), theirs),
                 "a VESSEL installation may not write another vessel's certificate");
    }
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 1);
}

QTEST_MAIN(TestCertificateCsvImport)
#include "tst_CertificateCsvImport.moc"
