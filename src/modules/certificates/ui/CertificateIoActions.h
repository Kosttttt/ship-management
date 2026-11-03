#pragma once

#include <QObject>
#include <QString>

class CertificateCsvExporter;
class CertificateCsvImporter;
class CertificateReportBuilder;
class QPushButton;
class QWidget;

// The certificate list's import and export buttons
// (certificate-import-export-spec §1): Import CSV, Export CSV and Export PDF.
//
// Split out of CertificateListWidget along the same seam
// CertificateListActions already uses, and for the same reason — the widget
// was already at the ~300-line guideline in CLAUDE.md §9. The distinction
// here is a different one, though: these three are driven by *the vessel*,
// not by which row is selected, so they sit beside Add Certificate in spirit
// even though they live in their own class.
//
// All three work in both installation modes and on exactly one vessel: the
// one already selected on screen. None of them is reachable with no vessel
// selected (§1/§9.20) — the buttons are disabled, and the page they live on
// is not even the one the list widget shows in that state.
//
// It owns its buttons, so the enablement rule lives next to the code that
// acts on them; the widget asks for them by accessor and places them in its
// own layout.
class CertificateIoActions : public QObject
{
    Q_OBJECT

public:
    // Parented to dialogParent, which is both the QObject owner and the
    // window every file dialog and message box raised from here is centred
    // on.
    CertificateIoActions(CertificateCsvExporter&   csvExporter,
                         CertificateCsvImporter&   csvImporter,
                         CertificateReportBuilder& reportBuilder,
                         QWidget*                  dialogParent);

    QPushButton* importCsvButton() const;
    QPushButton* exportCsvButton() const;
    QPushButton* exportPdfButton() const;

    // Pushed down from the widget rather than read back out of it, the same
    // way CertificateListActions is told which vessel is on screen.
    void setVesselId(const QString& vesselId);

    void updateButtonStates();

signals:
    // A successful import added or changed certificates, so the list is now
    // stale. The widget answers this by reloading, exactly as it does for the
    // selection-driven actions.
    void listChanged();

private:
    void importCsv();
    void exportCsv();
    void exportPdf();

    // Where a file dialog starts, and what it suggests calling the file.
    QString suggestedPath(const QString& fileName) const;

    CertificateCsvExporter&   m_csvExporter;
    CertificateCsvImporter&   m_csvImporter;
    CertificateReportBuilder& m_reportBuilder;

    QWidget* m_dialogParent = nullptr;
    QString  m_vesselId;

    QPushButton* m_importCsvButton = nullptr;
    QPushButton* m_exportCsvButton = nullptr;
    QPushButton* m_exportPdfButton = nullptr;
};
