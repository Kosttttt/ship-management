#include "CertificateTestSupport.h"
#include "CsvImportTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateState.h"
#include "modules/certificates/service/CertificateCsvExporter.h"
#include "modules/certificates/service/CertificateCsvImporter.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace CertificateTestSupport;
using namespace CsvImportTestSupport;

// certificate-import-export-spec §6 and §9.8 to §9.11: what the two
// survey-list columns do, and the mid-cycle case the list design exists for.
class TestCertificateCsvImportSurveys : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §9.8
    void everyDateInBothListsBecomesItsOwnEndorsement();

    // §9.9
    void aListForATrackTheCertificateDoesNotRequireCreatesNothing();

    // §9.10
    void anOverwriteLeavesTheSurveyHistoryUntouched();

    // §9.11 — the case v1.0 of the spec got wrong.
    void aMidCycleCertificateImportsWithTheCorrectSeverity();
    void asingleMostRecentDateWouldHaveReportedTheSameShipOverdue();

    // The REPLACES_ANNUAL merge, happening in the existing domain code with
    // no special case anywhere in the import path.
    void anIntermediateSurveyCanSatisfyAnAnnualUnderReplacesAnnual();
    void theSameDatesUnderAdditionalModeAreStillOverdue();

    // §6
    void aSurveyDateBeforeTheCertificateWasIssuedMakesTheRowInvalid();

    // §5.2
    void aFailurePartWayThroughWritesNothingAtAll();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      pathFor(const QString& name) { return m_folder.filePath(name); }

    // Imports one row and returns the certificate it created.
    Certificate importOneRow(const QHash<QString, QString>& values);

    int endorsementCountFor(const QString& certificateId);

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_folder;
};

void TestCertificateCsvImportSurveys::init()
{
    m_connectionName = QStringLiteral("csvsurveys_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_folder.isValid());

    m_vesselId      = QStringLiteral("v1");
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, m_vesselId, QStringLiteral("MV Aurora"),
                        QStringLiteral("9074729"))
                 .isEmpty());
}

void TestCertificateCsvImportSurveys::cleanup()
{
    closeDatabase(m_connectionName);
}

Certificate TestCertificateCsvImportSurveys::importOneRow(const QHash<QString, QString>& values)
{
    const QString path = writeCsv(pathFor(QStringLiteral("one.csv")), {rowWith(values)});
    if (path.isEmpty()) {
        return Certificate();
    }

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    if (!importer.buildPreview(m_vesselId, path, &preview)) {
        qWarning() << "preview refused:" << importer.errorString();
        return Certificate();
    }
    if (preview.hasInvalidRows()) {
        qWarning() << "row was invalid:" << preview.rows.first().reason;
        return Certificate();
    }
    if (!importer.runImport(m_vesselId, preview)) {
        qWarning() << "import failed:" << importer.errorString();
        return Certificate();
    }

    QList<Certificate> stored;
    if (!certificates.list(m_vesselId, &stored) || stored.size() != 1) {
        return Certificate();
    }
    return stored.first();
}

int TestCertificateCsvImportSurveys::endorsementCountFor(const QString& certificateId)
{
    QSqlDatabase db = database();
    QSqlQuery    query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM endorsement"
                                 " WHERE certificate_id = ? AND is_deleted = 0"));
    query.addBindValue(certificateId);
    if (!query.exec() || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void TestCertificateCsvImportSurveys::everyDateInBothListsBecomesItsOwnEndorsement()
{
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Construction"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Requires Intermediate Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Annual Surveys"),
                  QStringLiteral("2027-02-10;2028-02-04;2029-02-20"));
    values.insert(QStringLiteral("Intermediate Surveys"), QStringLiteral("2029-03-01"));

    const Certificate imported = importOneRow(values);
    QVERIFY(!imported.id.isEmpty());

    // §9.8: one endorsement per date across both lists — four, not one, and
    // nothing deduplicated away.
    QCOMPARE(endorsementCountFor(imported.id), 4);

    QSqlDatabase          db = database();
    EndorsementRepository endorsements(db, officeContext());
    QList<Endorsement>    stored;
    QVERIFY(endorsements.list(imported.id, &stored));

    QList<QDate> annual;
    QList<QDate> intermediate;
    for (const Endorsement& endorsement : stored) {
        if (endorsement.surveyType == SurveyType::Annual) {
            annual.append(endorsement.endorsementDate);
        } else if (endorsement.surveyType == SurveyType::Intermediate) {
            intermediate.append(endorsement.endorsementDate);
        }
    }
    QCOMPARE(annual.size(), 3);
    QCOMPARE(intermediate.size(), 1);
    QVERIFY(annual.contains(QDate(2027, 2, 10)));
    QVERIFY(annual.contains(QDate(2028, 2, 4)));
    QVERIFY(annual.contains(QDate(2029, 2, 20)));
    QCOMPARE(intermediate.first(), QDate(2029, 3, 1));
}

void TestCertificateCsvImportSurveys::aListForATrackTheCertificateDoesNotRequireCreatesNothing()
{
    QHash<QString, QString> values = validValues(QStringLiteral("Load Line"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Annual Surveys"), QStringLiteral("2027-02-10"));
    // Filled in, but the certificate requires no intermediate survey.
    values.insert(QStringLiteral("Intermediate Surveys"), QStringLiteral("2028-02-04"));

    const Certificate imported = importOneRow(values);
    QVERIFY(!imported.id.isEmpty());

    // §9.9: ignored, not an error, and certainly not written.
    QCOMPARE(endorsementCountFor(imported.id), 1);
}

void TestCertificateCsvImportSurveys::anOverwriteLeavesTheSurveyHistoryUntouched()
{
    // §9.10: a matched row that the user chooses to overwrite updates the
    // certificate's own fields and nothing else. This is what stops importing
    // the same file twice from doubling every survey record.
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), true, false);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 10));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    const QString path = pathFor(QStringLiteral("again.csv"));
    QVERIFY2(exporter.exportToFile(m_vesselId, path), qPrintable(exporter.errorString()));
    QCOMPARE(endorsementCountFor(QStringLiteral("c1")), 1);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QCOMPARE(preview.countOf(ImportRowOutcome::Match), 1);
    // The survey column was read and is available to show in the preview...
    QCOMPARE(preview.rows.first().parsed.annualSurveys.size(), 1);

    for (CertificateImportRow& row : preview.rows) {
        row.overwrite = true;
    }
    QVERIFY2(importer.runImport(m_vesselId, preview), qPrintable(importer.errorString()));

    // ...and was still never written.
    QCOMPARE(endorsementCountFor(QStringLiteral("c1")), 1);

    // Twice more, for good measure: re-importing the same export can never
    // accumulate duplicate surveys.
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    for (CertificateImportRow& row : preview.rows) {
        row.overwrite = true;
    }
    QVERIFY(importer.runImport(m_vesselId, preview));
    QCOMPARE(endorsementCountFor(QStringLiteral("c1")), 1);
}

void TestCertificateCsvImportSurveys::aMidCycleCertificateImportsWithTheCorrectSeverity()
{
    // §9.11. A five-year certificate expiring 2031-02-04 has anniversaries on
    // 2027, 2028, 2029 and 2030-02-04. Three annuals have genuinely been
    // done; the fourth window does not open until 2029-11-04. On 2029-06-01
    // this ship is entirely up to date.
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Construction"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Annual Surveys"),
                  QStringLiteral("2027-02-10;2028-02-04;2029-02-20"));

    const Certificate imported = importOneRow(values);
    QVERIFY(!imported.id.isEmpty());

    QSqlDatabase          db = database();
    EndorsementRepository endorsements(db, officeContext());
    QList<Endorsement>    stored;
    QVERIFY(endorsements.list(imported.id, &stored));
    QCOMPARE(stored.size(), 3);

    const QDate            today = QDate(2029, 6, 1);
    const CertificateState state =
        computeCertificateState(imported, stored, AlertThresholds(), today);

    QCOMPARE(state.survey, SurveySeverity::NotDue);
    QCOMPARE(state.display, DisplayStatus::Valid);
    QCOMPARE(state.nextAnniversary, QDate(2030, 2, 4));
    QVERIFY2(!state.reason.contains(QStringLiteral("late")),
             "nothing was late, so nothing may claim it was");
}

void TestCertificateCsvImportSurveys::asingleMostRecentDateWouldHaveReportedTheSameShipOverdue()
{
    // The regression this column design exists to prevent, shown directly:
    // SurveySchedule::evaluateAnnual() gives each anniversary the earliest
    // endorsement still unclaimed, so one date accounts for one past
    // anniversary — not for "where the schedule has got to".
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Construction"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Annual Surveys"),
                  QStringLiteral("2027-02-10;2028-02-04;2029-02-20"));

    const Certificate imported = importOneRow(values);
    QVERIFY(!imported.id.isEmpty());

    const QDate today = QDate(2029, 6, 1);

    // Only the most recent survey, as v1.0 of the spec would have imported.
    Endorsement onlyTheLatest;
    onlyTheLatest.certificateId   = imported.id;
    onlyTheLatest.surveyType      = SurveyType::Annual;
    onlyTheLatest.endorsementDate = QDate(2029, 2, 20);

    const CertificateState impoverished =
        computeCertificateState(imported, {onlyTheLatest}, AlertThresholds(), today);

    QCOMPARE(impoverished.survey, SurveySeverity::Overdue);
    QVERIFY2(impoverished.reason.contains(QStringLiteral("late")),
             "and it invents a late survey as well, from the one date it does have");

    // The full history, which is what this step actually imports.
    QSqlDatabase          db = database();
    EndorsementRepository endorsements(db, officeContext());
    QList<Endorsement>    stored;
    QVERIFY(endorsements.list(imported.id, &stored));
    QCOMPARE(computeCertificateState(imported, stored, AlertThresholds(), today).survey,
             SurveySeverity::NotDue);
}

void TestCertificateCsvImportSurveys::anIntermediateSurveyCanSatisfyAnAnnualUnderReplacesAnnual()
{
    // The annual windows are anchored on 2027, 2028, 2029 and 2030-02-04. An
    // intermediate survey on 2028-02-20 falls inside the second annual
    // window, which under REPLACES_ANNUAL satisfies that annual — handled
    // entirely by evaluateAnnual()'s existing merge, with nothing in the
    // import path knowing about it.
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Construction"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Requires Intermediate Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Intermediate Mode"), QStringLiteral("REPLACES_ANNUAL"));
    values.insert(QStringLiteral("Annual Surveys"), QStringLiteral("2027-02-10"));
    values.insert(QStringLiteral("Intermediate Surveys"), QStringLiteral("2028-02-20"));

    const Certificate imported = importOneRow(values);
    QVERIFY(!imported.id.isEmpty());
    QCOMPARE(imported.intermediateMode, IntermediateMode::ReplacesAnnual);
    QCOMPARE(endorsementCountFor(imported.id), 2);

    QSqlDatabase          db = database();
    EndorsementRepository endorsements(db, officeContext());
    QList<Endorsement>    stored;
    QVERIFY(endorsements.list(imported.id, &stored));

    const CertificateState state =
        computeCertificateState(imported, stored, AlertThresholds(), QDate(2028, 6, 1));

    QCOMPARE(state.survey, SurveySeverity::NotDue);
    QCOMPARE(state.nextAnniversary, QDate(2029, 2, 4));
}

void TestCertificateCsvImportSurveys::theSameDatesUnderAdditionalModeAreStillOverdue()
{
    // The counterfactual that proves the merge above actually did something:
    // identical dates, ADDITIONAL mode, and the 2028 annual has nothing to
    // satisfy it.
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Construction"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Requires Intermediate Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Intermediate Mode"), QStringLiteral("ADDITIONAL"));
    values.insert(QStringLiteral("Annual Surveys"), QStringLiteral("2027-02-10"));
    values.insert(QStringLiteral("Intermediate Surveys"), QStringLiteral("2028-02-20"));

    const Certificate imported = importOneRow(values);
    QVERIFY(!imported.id.isEmpty());
    QCOMPARE(imported.intermediateMode, IntermediateMode::Additional);

    QSqlDatabase          db = database();
    EndorsementRepository endorsements(db, officeContext());
    QList<Endorsement>    stored;
    QVERIFY(endorsements.list(imported.id, &stored));

    const CertificateState state =
        computeCertificateState(imported, stored, AlertThresholds(), QDate(2028, 6, 1));
    QCOMPARE(state.survey, SurveySeverity::Overdue);
}

void TestCertificateCsvImportSurveys::aSurveyDateBeforeTheCertificateWasIssuedMakesTheRowInvalid()
{
    // §6: the same rule the Add Endorsement dialog enforces, applied at
    // preview time so the row is never half-imported.
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Construction"));
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Annual Surveys"), QStringLiteral("2025-01-01"));

    const QString path = writeCsv(pathFor(QStringLiteral("early.csv")), {rowWith(values)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 1);
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::Invalid);
    QVERIFY(preview.rows.first().reason.contains(QStringLiteral("before the certificate")));

    QVERIFY(!importer.runImport(m_vesselId, preview));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateCsvImportSurveys::aFailurePartWayThroughWritesNothingAtAll()
{
    // §5.2: one transaction around the whole row loop. Driven by handing
    // runImport() a preview that was not built by buildPreview() — the only
    // way to reach a write that fails, since the preview is what normally
    // catches this.
    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;

    CertificateImportRow good;
    good.rowNumber                       = 1;
    good.outcome                         = ImportRowOutcome::New;
    good.parsed.certificate              = sampleCertificate(m_vesselId,
                                                             QStringLiteral("Good One"));
    preview.rows.append(good);

    CertificateImportRow broken = good;
    broken.rowNumber            = 2;
    // Refused by validateCertificate(), inside create(), half-way through the
    // loop — after the first row has already been written.
    broken.parsed.certificate.name = QString();
    preview.rows.append(broken);

    QVERIFY2(!importer.runImport(m_vesselId, preview),
             "a failure anywhere in the loop fails the whole import");
    QVERIFY(importer.errorString().contains(QStringLiteral("Row 2")));

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

QTEST_MAIN(TestCertificateCsvImportSurveys)
#include "tst_CertificateCsvImportSurveys.moc"
