#pragma once

#include <QDateTime>
#include <QString>

class AppSettingRepository;
class CertificateExtensionRepository;
class CertificateRepository;
class EndorsementRepository;
class VesselRepository;

// One vessel's certificate list as a printable statement
// (certificate-import-export-spec §8).
//
// A cut-down version of what certificate-control-spec.md §9 plans for the
// reporting step: no fleet view, no filtering, no per-certificate history —
// just "here is every certificate this ship holds and where each one stands,
// as of now," for handing to a surveyor or a port state inspector.
//
// Read-only. It needs five collaborators because the "current status" column
// is computed, never stored (CLAUDE.md §6.7): the certificates and their
// endorsements, the active short-term extension, the alert thresholds that
// decide what counts as "soon", and the vessel for the heading. These are
// exactly the collaborators CertificateListWidget already needs to draw the
// same column, which is what keeps the paper and the screen in agreement.
class CertificateReportBuilder
{
public:
    CertificateReportBuilder(CertificateRepository&          certificates,
                             EndorsementRepository&          endorsements,
                             CertificateExtensionRepository& extensions,
                             AppSettingRepository&           appSettings,
                             VesselRepository&               vessels);

    // The document as HTML, which is what QTextDocument renders
    // (CLAUDE.md §2: QTextDocument + QPrinter, never a third-party reporting
    // library).
    //
    // generatedAt is a parameter rather than a clock read inside, for the same
    // reason computeCertificateState() takes `today`: it is the only way to
    // test what the document says. Its date is also the "today" every status
    // in the document is computed against, so the heading and the statuses
    // can never describe different days.
    bool buildHtml(const QString& vesselId, const QDateTime& generatedAt, QString* html);

    // Reads the clock once and writes a PDF.
    bool exportToPdf(const QString& vesselId, const QString& filePath);

    QString errorString() const;

private:
    CertificateRepository&          m_certificates;
    EndorsementRepository&          m_endorsements;
    CertificateExtensionRepository& m_extensions;
    AppSettingRepository&           m_appSettings;
    VesselRepository&               m_vessels;
    QString                         m_errorString;
};
