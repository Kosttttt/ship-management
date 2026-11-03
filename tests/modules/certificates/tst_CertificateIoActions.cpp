#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateCsvImporter.h"
#include "modules/certificates/ui/CertificateImportPreviewDialog.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-import-export-spec §1, §5.5, §9.19 and §9.20: the entry points
// on the certificate list, and the preview dialog's own rules.
class TestCertificateIoActions : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theListOffersImportCsvExportCsvAndExportPdf();

    // §9.20
    void allThreeAreDisabledWithNoVesselSelected();
    void theListPageIsNotEvenShownWithNoVesselSelected();
    void allThreeBecomeAvailableOnceAVesselIsSelected();
    void selectingNothingAgainDisablesThemAgain();

    // §9.19
    void allThreeAreAvailableInVesselModeWithoutAnyPicker();

    // §5.5
    void thePreviewShowsOneRowPerFileRowWithItsOutcome();
    void importIsBlockedWhileAnyRowIsInvalid();
    void importIsAvailableWhenEveryRowIsNewOrMatched();
    void importIsUnavailableForAnEmptyFile();
    void onlyAMatchingRowOffersAnOverwriteTick();
    void theDefaultForAMatchIsSkip();
    void tickingOverwriteIsReportedBackInTheDecisions();
    void aPreviouslyDeletedRowIsLabelledApartFromPlainNew();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static CertificateImportRow rowOf(int number, ImportRowOutcome outcome,
                                      const QString& name);

    QString m_connectionName;
};

void TestCertificateIoActions::init()
{
    m_connectionName = QStringLiteral("ioactions_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateIoActions::cleanup()
{
    closeDatabase(m_connectionName);
}

CertificateImportRow TestCertificateIoActions::rowOf(int number, ImportRowOutcome outcome,
                                                     const QString& name)
{
    CertificateImportRow row;
    row.rowNumber                      = number;
    row.outcome                        = outcome;
    row.parsed.certificate.name        = name;
    row.parsed.certificate.listNumber  = QString::number(number);
    if (outcome == ImportRowOutcome::Match) {
        row.matchedId = QStringLiteral("c%1").arg(number);
    }
    if (outcome == ImportRowOutcome::Invalid) {
        row.reason = QStringLiteral("An issue date is required.");
    }
    return row;
}

void TestCertificateIoActions::theListOffersImportCsvExportCsvAndExportPdf()
{
    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture     attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);

    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("certificateImportCsvButton"))
            != nullptr);
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("certificateExportCsvButton"))
            != nullptr);
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("certificateExportPdfButton"))
            != nullptr);
}

void TestCertificateIoActions::allThreeAreDisabledWithNoVesselSelected()
{
    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture     attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);

    // §1/§9.20: one operation, one vessel. With nothing selected there is no
    // vessel to scope either direction to.
    for (const QString& name : {QStringLiteral("certificateImportCsvButton"),
                                QStringLiteral("certificateExportCsvButton"),
                                QStringLiteral("certificateExportPdfButton")}) {
        auto* button = widget.findChild<QPushButton*>(name);
        QVERIFY(button != nullptr);
        QVERIFY2(!button->isEnabled(), qPrintable(name));
    }
}

void TestCertificateIoActions::theListPageIsNotEvenShownWithNoVesselSelected()
{
    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture     attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);

    auto* stack = widget.findChild<QStackedWidget*>(QStringLiteral("certificateStack"));
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->currentIndex(), 0); // the "select a vessel" prompt
}

void TestCertificateIoActions::allThreeBecomeAvailableOnceAVesselIsSelected()
{
    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture     attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);

    widget.setVesselId(QStringLiteral("v1"));

    for (const QString& name : {QStringLiteral("certificateImportCsvButton"),
                                QStringLiteral("certificateExportCsvButton"),
                                QStringLiteral("certificateExportPdfButton")}) {
        auto* button = widget.findChild<QPushButton*>(name);
        QVERIFY(button != nullptr);
        QVERIFY2(button->isEnabled(), qPrintable(name));
    }

    // And they do not depend on a certificate being selected, the way Renew
    // and Delete do: an empty vessel is exactly when an import is wanted.
    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("certificateImportCsvButton"))
                ->isEnabled());
}

void TestCertificateIoActions::selectingNothingAgainDisablesThemAgain()
{
    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture     attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);

    widget.setVesselId(QStringLiteral("v1"));
    widget.setVesselId(QString());

    auto* button = widget.findChild<QPushButton*>(QStringLiteral("certificateExportCsvButton"));
    QVERIFY(button != nullptr);
    QVERIFY(!button->isEnabled());
}

void TestCertificateIoActions::allThreeAreAvailableInVesselModeWithoutAnyPicker()
{
    // §9.19: scope is automatic. The widget learns its vessel from the
    // installation at construction and never asks again.
    QSqlDatabase                   db = database();
    const InstallationContext      context = vesselContext(QStringLiteral("v1"));
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture     attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);

    QCOMPARE(widget.vesselId(), QStringLiteral("v1"));
    for (const QString& name : {QStringLiteral("certificateImportCsvButton"),
                                QStringLiteral("certificateExportCsvButton"),
                                QStringLiteral("certificateExportPdfButton")}) {
        auto* button = widget.findChild<QPushButton*>(name);
        QVERIFY(button != nullptr);
        QVERIFY2(button->isEnabled(), qPrintable(name));
    }
}

void TestCertificateIoActions::thePreviewShowsOneRowPerFileRowWithItsOutcome()
{
    CertificateImportPreview preview;
    preview.rows.append(rowOf(1, ImportRowOutcome::New, QStringLiteral("Load Line")));
    preview.rows.append(rowOf(2, ImportRowOutcome::Match, QStringLiteral("Safety Radio")));

    CertificateImportPreviewDialog dialog(preview);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Load Line"));
    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("New"));
    QCOMPARE(table->item(1, 3)->text(), QStringLiteral("Match found"));
}

void TestCertificateIoActions::importIsBlockedWhileAnyRowIsInvalid()
{
    CertificateImportPreview preview;
    preview.rows.append(rowOf(1, ImportRowOutcome::New, QStringLiteral("Load Line")));
    preview.rows.append(rowOf(2, ImportRowOutcome::Invalid, QStringLiteral("Broken")));

    CertificateImportPreviewDialog dialog(preview);
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("importConfirmButton"));
    QVERIFY(button != nullptr);
    QVERIFY2(!button->isEnabled(),
             "§5.5: the whole import is blocked, not just the bad row skipped");

    // And the reason is on screen, on that row, rather than in a dialog the
    // user has already dismissed.
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    QVERIFY(table != nullptr);
    QVERIFY(table->item(1, 5)->text().contains(QStringLiteral("issue date")));
}

void TestCertificateIoActions::importIsAvailableWhenEveryRowIsNewOrMatched()
{
    CertificateImportPreview preview;
    preview.rows.append(rowOf(1, ImportRowOutcome::New, QStringLiteral("Load Line")));
    preview.rows.append(rowOf(2, ImportRowOutcome::Match, QStringLiteral("Safety Radio")));
    preview.rows.append(
        rowOf(3, ImportRowOutcome::NewPreviouslyDeleted, QStringLiteral("Old One")));

    CertificateImportPreviewDialog dialog(preview);
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("importConfirmButton"));
    QVERIFY(button != nullptr);
    QVERIFY(button->isEnabled());
}

void TestCertificateIoActions::importIsUnavailableForAnEmptyFile()
{
    const CertificateImportPreview empty;
    CertificateImportPreviewDialog dialog(empty);
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("importConfirmButton"));
    QVERIFY(button != nullptr);
    QVERIFY(!button->isEnabled());
}

void TestCertificateIoActions::onlyAMatchingRowOffersAnOverwriteTick()
{
    CertificateImportPreview preview;
    preview.rows.append(rowOf(1, ImportRowOutcome::New, QStringLiteral("Load Line")));
    preview.rows.append(rowOf(2, ImportRowOutcome::Match, QStringLiteral("Safety Radio")));

    CertificateImportPreviewDialog dialog(preview);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    QVERIFY(table != nullptr);

    // A new row has no decision to make, so it gets no box that looks like
    // one.
    QVERIFY(!table->item(0, 4)->flags().testFlag(Qt::ItemIsUserCheckable));
    QVERIFY(table->item(1, 4)->flags().testFlag(Qt::ItemIsUserCheckable));
}

void TestCertificateIoActions::theDefaultForAMatchIsSkip()
{
    CertificateImportPreview preview;
    preview.rows.append(rowOf(1, ImportRowOutcome::Match, QStringLiteral("Safety Radio")));

    CertificateImportPreviewDialog dialog(preview);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->item(0, 4)->checkState(), Qt::Unchecked);
    QVERIFY2(!dialog.decisions().rows.first().overwrite,
             "an accidental Enter must overwrite nothing");
}

void TestCertificateIoActions::tickingOverwriteIsReportedBackInTheDecisions()
{
    CertificateImportPreview preview;
    preview.rows.append(rowOf(1, ImportRowOutcome::Match, QStringLiteral("One")));
    preview.rows.append(rowOf(2, ImportRowOutcome::Match, QStringLiteral("Two")));

    CertificateImportPreviewDialog dialog(preview);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    QVERIFY(table != nullptr);
    table->item(1, 4)->setCheckState(Qt::Checked);

    const CertificateImportPreview decided = dialog.decisions();
    QCOMPARE(decided.rows.size(), 2);
    QVERIFY(!decided.rows.at(0).overwrite);
    QVERIFY(decided.rows.at(1).overwrite);
    // Everything else about the row survives the trip back.
    QCOMPARE(decided.rows.at(1).matchedId, QStringLiteral("c2"));
    QCOMPARE(decided.rows.at(1).parsed.certificate.name, QStringLiteral("Two"));
}

void TestCertificateIoActions::aPreviouslyDeletedRowIsLabelledApartFromPlainNew()
{
    // §5.1: so that the resurrected certificate getting a different Id than
    // the file's does not read as a bug.
    CertificateImportPreview preview;
    preview.rows.append(
        rowOf(1, ImportRowOutcome::NewPreviouslyDeleted, QStringLiteral("Old One")));

    CertificateImportPreviewDialog dialog(preview);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("importPreviewTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("New (previously deleted)"));
    QVERIFY(table->item(0, 5)->text().contains(QStringLiteral("deleted")));
}

QTEST_MAIN(TestCertificateIoActions)
#include "tst_CertificateIoActions.moc"
