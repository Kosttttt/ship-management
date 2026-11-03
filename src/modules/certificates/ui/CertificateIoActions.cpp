#include "modules/certificates/ui/CertificateIoActions.h"

#include "modules/certificates/service/CertificateCsvExporter.h"
#include "modules/certificates/service/CertificateCsvImporter.h"
#include "modules/certificates/service/CertificateReportBuilder.h"
#include "modules/certificates/ui/CertificateImportPreviewDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>

namespace {

const QString kCsvFilter = QStringLiteral("Comma-separated values (*.csv)");
const QString kPdfFilter = QStringLiteral("PDF documents (*.pdf)");

} // namespace

CertificateIoActions::CertificateIoActions(CertificateCsvExporter&   csvExporter,
                                           CertificateCsvImporter&   csvImporter,
                                           CertificateReportBuilder& reportBuilder,
                                           QWidget*                  dialogParent)
    : QObject(dialogParent)
    , m_csvExporter(csvExporter)
    , m_csvImporter(csvImporter)
    , m_reportBuilder(reportBuilder)
    , m_dialogParent(dialogParent)
{
    m_importCsvButton = new QPushButton(tr("&Import CSV..."), dialogParent);
    m_importCsvButton->setObjectName(QStringLiteral("certificateImportCsvButton"));

    m_exportCsvButton = new QPushButton(tr("Export &CSV..."), dialogParent);
    m_exportCsvButton->setObjectName(QStringLiteral("certificateExportCsvButton"));

    m_exportPdfButton = new QPushButton(tr("Export &PDF..."), dialogParent);
    m_exportPdfButton->setObjectName(QStringLiteral("certificateExportPdfButton"));

    connect(m_importCsvButton, &QPushButton::clicked, this, &CertificateIoActions::importCsv);
    connect(m_exportCsvButton, &QPushButton::clicked, this, &CertificateIoActions::exportCsv);
    connect(m_exportPdfButton, &QPushButton::clicked, this, &CertificateIoActions::exportPdf);

    updateButtonStates();
}

QPushButton* CertificateIoActions::importCsvButton() const
{
    return m_importCsvButton;
}

QPushButton* CertificateIoActions::exportCsvButton() const
{
    return m_exportCsvButton;
}

QPushButton* CertificateIoActions::exportPdfButton() const
{
    return m_exportPdfButton;
}

void CertificateIoActions::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
    updateButtonStates();
}

void CertificateIoActions::updateButtonStates()
{
    // §1: one operation, one vessel. With nothing selected there is no vessel
    // to scope either direction to, so all three are simply unavailable
    // rather than guessing at the fleet.
    const bool haveVessel = !m_vesselId.isEmpty();
    m_importCsvButton->setEnabled(haveVessel);
    m_exportCsvButton->setEnabled(haveVessel);
    m_exportPdfButton->setEnabled(haveVessel);
}

QString CertificateIoActions::suggestedPath(const QString& fileName) const
{
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        return fileName;
    }
    return QDir(documents).filePath(fileName);
}

void CertificateIoActions::exportCsv()
{
    if (m_vesselId.isEmpty()) {
        return;
    }

    const QString path =
        QFileDialog::getSaveFileName(m_dialogParent, tr("Export Certificates"),
                                     suggestedPath(QStringLiteral("certificates.csv")),
                                     kCsvFilter);
    if (path.isEmpty()) {
        return; // cancelled
    }

    if (!m_csvExporter.exportToFile(m_vesselId, path)) {
        QMessageBox::warning(m_dialogParent, tr("Could not export"),
                             m_csvExporter.errorString());
        return;
    }

    QMessageBox::information(m_dialogParent, tr("Export complete"),
                             tr("The certificate list was written to:\n%1").arg(path));
}

void CertificateIoActions::exportPdf()
{
    if (m_vesselId.isEmpty()) {
        return;
    }

    const QString path =
        QFileDialog::getSaveFileName(m_dialogParent, tr("Export Certificate Statement"),
                                     suggestedPath(QStringLiteral("certificates.pdf")),
                                     kPdfFilter);
    if (path.isEmpty()) {
        return;
    }

    if (!m_reportBuilder.exportToPdf(m_vesselId, path)) {
        QMessageBox::warning(m_dialogParent, tr("Could not export"),
                             m_reportBuilder.errorString());
        return;
    }

    QMessageBox::information(m_dialogParent, tr("Export complete"),
                             tr("The certificate statement was written to:\n%1").arg(path));
}

void CertificateIoActions::importCsv()
{
    if (m_vesselId.isEmpty()) {
        return;
    }

    const QString path =
        QFileDialog::getOpenFileName(m_dialogParent, tr("Import Certificates"),
                                     suggestedPath(QString()), kCsvFilter);
    if (path.isEmpty()) {
        return;
    }

    // A file-level refusal (§3/§7): nothing is shown and nothing is written,
    // because the problem is with the file rather than with one row of it.
    CertificateImportPreview preview;
    if (!m_csvImporter.buildPreview(m_vesselId, path, &preview)) {
        QMessageBox::warning(m_dialogParent, tr("Could not import that file"),
                             m_csvImporter.errorString());
        return;
    }

    if (preview.rows.isEmpty()) {
        QMessageBox::information(m_dialogParent, tr("Nothing to import"),
                                 tr("That file has a header row but no certificates."));
        return;
    }

    CertificateImportPreviewDialog dialog(preview, m_dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return; // the preview is a dry run; cancelling writes nothing
    }

    int created = 0;
    int updated = 0;
    if (!m_csvImporter.runImport(m_vesselId, dialog.decisions(), &created, &updated)) {
        QMessageBox::warning(m_dialogParent, tr("Could not import"),
                             m_csvImporter.errorString());
        return;
    }

    emit listChanged();

    QMessageBox::information(m_dialogParent, tr("Import complete"),
                             tr("%1 certificate(s) added, %2 updated.")
                                 .arg(created)
                                 .arg(updated));
}
