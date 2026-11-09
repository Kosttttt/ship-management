#include "CertificateTestSupport.h"
#include "CsvImportTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateCsvFormat.h"
#include "modules/certificates/service/CertificateCsvImporter.h"
#include "modules/certificates/service/CertificateIssueDatePlaceholder.h"
#include "modules/certificates/ui/CertificateImportPreviewDialog.h"

#include <QCheckBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace CertificateTestSupport;
using namespace CsvImportTestSupport;

// certificate-list-usability-fixes.md §3: the opt-in placeholder issue date.
class TestCertificateImportPlaceholder : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // The rule itself, with no database and no dialog.
    void aMissingIssueDateIsStillInvalidByDefault();
    void withThePlaceholderAllowedTheRowParses();
    void theRowCarriesTheFlagInItsNotes();
    void theNoteIsPrependedToTheRowsOwnNotes();
    void anUnreadableIssueDateIsStillRefused();
    void aRowWithAnyOtherProblemStaysInvalid();
    void theNoExpiryRuleStillAppliesToAPatchedRow();
    void aSurveyDateBeforeThePlaceholderIsRefused();

    // Re-evaluating an open preview.
    void tickingTheOptionRescuesOnlyTheRowsItShould();
    void untickingTheOptionPutsThemBackAsInvalid();
    void aRescuedRowKeepsTheClassificationItsIdGivesIt();
    void overwriteChoicesSurviveAReEvaluation();
    void importingARescuedRowStoresThePlaceholderAndTheNote();

    // The dialog's one job.
    void theCheckboxStartsUnticked();
    void tickingTheCheckboxUnblocksImport();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      pathFor(const QString& name) { return m_folder.filePath(name); }

    // The row every format test starts from: valid apart from the Issue Date
    // cell, which is deliberately left empty.
    static QHash<QString, QString> rowWithNoIssueDate();
    static bool parse(const QHash<QString, QString>& values, bool allowPlaceholder,
                      CertificateCsvRow* row, QString* reason);

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_folder;
};

void TestCertificateImportPlaceholder::init()
{
    m_connectionName = QStringLiteral("placeholder_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_folder.isValid());

    m_vesselId      = QStringLiteral("v1");
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, m_vesselId, QStringLiteral("MV Aurora"),
                        QStringLiteral("9074729"))
                 .isEmpty());
}

void TestCertificateImportPlaceholder::cleanup()
{
    closeDatabase(m_connectionName);
}

QHash<QString, QString> TestCertificateImportPlaceholder::rowWithNoIssueDate()
{
    // The real case behind §3: an item with an expiry date the fleet wants
    // tracked, and no issue date because there never was one.
    QHash<QString, QString> values = validValues(QStringLiteral("Lifeboat Davit Service"));
    values.insert(QStringLiteral("Issue Date"), QString());
    return values;
}

bool TestCertificateImportPlaceholder::parse(const QHash<QString, QString>& values,
                                             bool allowPlaceholder, CertificateCsvRow* row,
                                             QString* reason)
{
    QHash<QString, int> columnIndex;
    QString             error;
    if (!CertificateCsvFormat::mapHeader(CertificateCsvFormat::headerRow(), &columnIndex,
                                         &error)) {
        qWarning() << "the spec's own header should map:" << error;
        return false;
    }
    return CertificateCsvFormat::parseRow(rowWith(values), columnIndex, row, reason,
                                          allowPlaceholder);
}

void TestCertificateImportPlaceholder::aMissingIssueDateIsStillInvalidByDefault()
{
    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(!parse(rowWithNoIssueDate(), false, &row, &reason),
             "§3: the default must stay strict - a missing issue date is often a real slip");
    QVERIFY(reason.contains(QStringLiteral("issue date")));
}

void TestCertificateImportPlaceholder::withThePlaceholderAllowedTheRowParses()
{
    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(parse(rowWithNoIssueDate(), true, &row, &reason), qPrintable(reason));
    QCOMPARE(row.certificate.issueDate, QDate(1990, 1, 1));
    QCOMPARE(row.certificate.issueDate, CertificateIssueDatePlaceholder::date());
}

void TestCertificateImportPlaceholder::theRowCarriesTheFlagInItsNotes()
{
    CertificateCsvRow row;
    QString           reason;
    QVERIFY(parse(rowWithNoIssueDate(), true, &row, &reason));

    // §3: the note is the only thing that will ever tell anyone later that
    // the date is not a fact, so it has to be on the certificate itself and
    // not merely logged somewhere.
    QVERIFY(!row.certificate.notes.isEmpty());
    QCOMPARE(row.certificate.notes, CertificateIssueDatePlaceholder::note());
    QVERIFY(row.certificate.notes.contains(QStringLiteral("placeholder")));
    QVERIFY(row.certificate.notes.contains(QStringLiteral("1990-01-01")));
}

void TestCertificateImportPlaceholder::theNoteIsPrependedToTheRowsOwnNotes()
{
    QHash<QString, QString> values = rowWithNoIssueDate();
    values.insert(QStringLiteral("Notes"), QStringLiteral("Serviced in Rotterdam"));

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(parse(values, true, &row, &reason));

    QVERIFY2(row.certificate.notes.startsWith(CertificateIssueDatePlaceholder::note()),
             "the flag comes first - it is what someone opening this needs to see");
    QVERIFY2(row.certificate.notes.contains(QStringLiteral("Serviced in Rotterdam")),
             "and the row's own note is kept, not replaced");
}

void TestCertificateImportPlaceholder::anUnreadableIssueDateIsStillRefused()
{
    // The distinction §3 turns on: an empty cell is a known gap, a cell with
    // something unreadable in it is a typo. Only the first can be patched.
    QHash<QString, QString> values = validValues(QStringLiteral("Safety Radio"));
    values.insert(QStringLiteral("Issue Date"), QStringLiteral("04/02/2026"));

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(!parse(values, true, &row, &reason),
             "a mistyped date must not be quietly replaced with a placeholder");
    QVERIFY(reason.contains(QStringLiteral("04/02/2026")));
}

void TestCertificateImportPlaceholder::aRowWithAnyOtherProblemStaysInvalid()
{
    QHash<QString, QString> values = rowWithNoIssueDate();
    values.insert(QStringLiteral("Category"), QStringLiteral("NONSENSE"));

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(!parse(values, true, &row, &reason),
             "§3 rescues one specific case, not every Invalid row");
    QVERIFY(reason.contains(QStringLiteral("NONSENSE")));
}

void TestCertificateImportPlaceholder::theNoExpiryRuleStillAppliesToAPatchedRow()
{
    // The placeholder is applied before the shared rules run, so a patched
    // row is judged exactly as a hand-entered one would be.
    QHash<QString, QString> values = rowWithNoIssueDate();
    values.insert(QStringLiteral("Expiry Date"), QString());
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!parse(values, true, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("no expiry")));
}

void TestCertificateImportPlaceholder::aSurveyDateBeforeThePlaceholderIsRefused()
{
    // Same point from the other side: the survey lists are checked against
    // the placeholder as this row's issue date, not against nothing.
    QHash<QString, QString> values = rowWithNoIssueDate();
    values.insert(QStringLiteral("Requires Annual Survey"), QStringLiteral("Yes"));
    values.insert(QStringLiteral("Annual Surveys"), QStringLiteral("1989-06-01"));

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!parse(values, true, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("before the certificate")));
}

void TestCertificateImportPlaceholder::tickingTheOptionRescuesOnlyTheRowsItShould()
{
    QHash<QString, QString> badCategory = rowWithNoIssueDate();
    badCategory.insert(QStringLiteral("Category"), QStringLiteral("NONSENSE"));

    const QString path = writeCsv(pathFor(QStringLiteral("mixed.csv")),
                                  {rowWith(validValues(QStringLiteral("Good One"))),
                                   rowWith(rowWithNoIssueDate()), rowWith(badCategory)});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY2(importer.buildPreview(m_vesselId, path, &preview),
             qPrintable(importer.errorString()));
    QCOMPARE(preview.rows.size(), 3);
    QCOMPARE(preview.countOf(ImportRowOutcome::Invalid), 2);

    applyIssueDatePlaceholder(&preview, true);

    QCOMPARE(preview.rows.at(0).outcome, ImportRowOutcome::New);
    QCOMPARE(preview.rows.at(1).outcome, ImportRowOutcome::New);
    QCOMPARE(preview.rows.at(2).outcome, ImportRowOutcome::Invalid);
    QCOMPARE(preview.countOf(ImportRowOutcome::Invalid), 1);
    QCOMPARE(preview.rows.at(1).parsed.certificate.issueDate, QDate(1990, 1, 1));

    // And the vessel is still the selected one, never anything from the file.
    QCOMPARE(preview.rows.at(1).parsed.certificate.vesselId, m_vesselId);
}

void TestCertificateImportPlaceholder::untickingTheOptionPutsThemBackAsInvalid()
{
    const QString path = writeCsv(pathFor(QStringLiteral("one.csv")),
                                  {rowWith(rowWithNoIssueDate())});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));

    applyIssueDatePlaceholder(&preview, true);
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::New);

    applyIssueDatePlaceholder(&preview, false);
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::Invalid);
    QVERIFY(!preview.rows.first().reason.isEmpty());
}

void TestCertificateImportPlaceholder::aRescuedRowKeepsTheClassificationItsIdGivesIt()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Already Here"));

    QHash<QString, QString> values = rowWithNoIssueDate();
    values.insert(QStringLiteral("Id"), QStringLiteral("c1"));

    const QString path = writeCsv(pathFor(QStringLiteral("match.csv")), {rowWith(values)});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::Invalid);

    applyIssueDatePlaceholder(&preview, true);

    // Not New: the Id still names a certificate this vessel has, and the
    // placeholder has nothing to do with matching.
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::Match);
    QCOMPARE(preview.rows.first().matchedId, QStringLiteral("c1"));
}

void TestCertificateImportPlaceholder::overwriteChoicesSurviveAReEvaluation()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Already Here"));

    QHash<QString, QString> matching = validValues(QStringLiteral("Already Here"));
    matching.insert(QStringLiteral("Id"), QStringLiteral("c1"));

    const QString path = writeCsv(pathFor(QStringLiteral("ticks.csv")),
                                  {rowWith(matching), rowWith(rowWithNoIssueDate())});

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    QCOMPARE(preview.rows.first().outcome, ImportRowOutcome::Match);

    // What the user had already decided before reaching for the checkbox.
    preview.rows[0].overwrite = true;

    applyIssueDatePlaceholder(&preview, true);

    QVERIFY2(preview.rows.at(0).overwrite,
             "re-evaluating must not quietly turn a chosen overwrite back into a skip");
}

void TestCertificateImportPlaceholder::importingARescuedRowStoresThePlaceholderAndTheNote()
{
    const QString path = writeCsv(pathFor(QStringLiteral("rescued.csv")),
                                  {rowWith(rowWithNoIssueDate())});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));
    applyIssueDatePlaceholder(&preview, true);
    QVERIFY(!preview.hasInvalidRows());

    QVERIFY2(importer.runImport(m_vesselId, preview), qPrintable(importer.errorString()));

    QList<Certificate> stored;
    QVERIFY(certificates.list(m_vesselId, &stored));
    QCOMPARE(stored.size(), 1);

    // §3: the note has to survive into what is actually written, because it
    // is the only record that the date is not real.
    QCOMPARE(stored.first().issueDate, QDate(1990, 1, 1));
    QVERIFY(stored.first().notes.contains(QStringLiteral("placeholder")));
    QCOMPARE(stored.first().name, QStringLiteral("Lifeboat Davit Service"));
}

void TestCertificateImportPlaceholder::theCheckboxStartsUnticked()
{
    const QString path = writeCsv(pathFor(QStringLiteral("one.csv")),
                                  {rowWith(rowWithNoIssueDate())});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));

    CertificateImportPreviewDialog dialog(preview);
    auto* check =
        dialog.findChild<QCheckBox*>(QStringLiteral("importPlaceholderIssueDateCheck"));
    QVERIFY(check != nullptr);
    QVERIFY2(!check->isChecked(), "§3: opt-in, so a bad import is still caught by default");

    auto* importButton = dialog.findChild<QPushButton*>(QStringLiteral("importConfirmButton"));
    QVERIFY(importButton != nullptr);
    QVERIFY2(!importButton->isEnabled(), "the row is Invalid until the box is ticked");
}

void TestCertificateImportPlaceholder::tickingTheCheckboxUnblocksImport()
{
    const QString path = writeCsv(pathFor(QStringLiteral("one.csv")),
                                  {rowWith(validValues(QStringLiteral("Good One"))),
                                   rowWith(rowWithNoIssueDate())});

    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvImporter importer(db, certificates, endorsements, vessels);

    CertificateImportPreview preview;
    QVERIFY(importer.buildPreview(m_vesselId, path, &preview));

    CertificateImportPreviewDialog dialog(preview);
    auto* table   = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    auto* check   = dialog.findChild<QCheckBox*>(
        QStringLiteral("importPlaceholderIssueDateCheck"));
    auto* confirm = dialog.findChild<QPushButton*>(QStringLiteral("importConfirmButton"));
    QVERIFY(table != nullptr);
    QVERIFY(check != nullptr);
    QVERIFY(confirm != nullptr);

    QCOMPARE(table->item(1, 3)->text(), QStringLiteral("Invalid"));
    QVERIFY(!confirm->isEnabled());

    // §3: it takes effect in the table already open, with no re-picking the
    // file.
    check->setChecked(true);

    QCOMPARE(table->item(1, 3)->text(), QStringLiteral("New"));
    QVERIFY(confirm->isEnabled());

    // And the decision the dialog hands back carries the patched row.
    const CertificateImportPreview decided = dialog.decisions();
    QCOMPARE(decided.rows.at(1).parsed.certificate.issueDate, QDate(1990, 1, 1));
}

QTEST_MAIN(TestCertificateImportPlaceholder)
#include "tst_CertificateImportPlaceholder.moc"
