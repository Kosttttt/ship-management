#include "modules/certificates/service/CertificateReportBuilder.h"

#include "core/AppSetting.h"
#include "core/AppSettingRepository.h"
#include "core/VesselRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateExtension.h"
#include "modules/certificates/domain/CertificateState.h"

#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTextDocument>

#include <algorithm>

namespace {

// The same format every date on screen uses, so the paper reads like the app.
const QString kDateFormat = QStringLiteral("dd MMM yyyy");

QString formatDate(const QDate& date)
{
    return date.isValid() ? date.toString(kDateFormat) : QString();
}

QString categoryLabel(CertificateCategory category)
{
    // The stored code, which is what the certificate list's own Category
    // column shows. Deliberately not prettified: STATUTORY and CLASS are the
    // words the industry uses.
    return CertificateCodes::categoryToCode(category);
}

// Mirrors the Expiry Date cell on the list screen: the certificate's own
// date, or that it never expires, with an active short-term extension taking
// the cell over (certificate-extension-spec §7). Spelled out here rather than
// shared with the widget because this is one table cell's wording, and a
// service cannot include a ui/ header.
QString expiryText(const Certificate& certificate, const CertificateState& state)
{
    if (state.activeExtensionExpiry.has_value()) {
        return QStringLiteral("%1 (short term)")
            .arg(state.activeExtensionExpiry->toString(kDateFormat));
    }
    if (certificate.neverExpires()) {
        return QStringLiteral("Does not expire");
    }
    return formatDate(certificate.expiryDate);
}

QString cell(const QString& text)
{
    return QStringLiteral("<td>%1</td>").arg(text.toHtmlEscaped());
}

QString headerCell(const QString& text)
{
    return QStringLiteral("<th>%1</th>").arg(text.toHtmlEscaped());
}

} // namespace

CertificateReportBuilder::CertificateReportBuilder(CertificateRepository& certificates,
                                                   EndorsementRepository& endorsements,
                                                   CertificateExtensionRepository& extensions,
                                                   AppSettingRepository&           appSettings,
                                                   VesselRepository&               vessels)
    : m_certificates(certificates)
    , m_endorsements(endorsements)
    , m_extensions(extensions)
    , m_appSettings(appSettings)
    , m_vessels(vessels)
{
}

bool CertificateReportBuilder::buildHtml(const QString&   vesselId,
                                         const QDateTime& generatedAt,
                                         QString*         html)
{
    m_errorString.clear();
    html->clear();

    std::optional<Vessel> vessel;
    if (!m_vessels.findById(vesselId, &vessel)) {
        m_errorString = m_vessels.errorString();
        return false;
    }
    if (!vessel.has_value()) {
        m_errorString = QStringLiteral("That vessel could not be found.");
        return false;
    }

    QList<Certificate> certificates;
    if (!m_certificates.list(vesselId, &certificates)) {
        m_errorString = m_certificates.errorString();
        return false;
    }

    // The same order as the CSV and as the screen (§4/§8): by reference
    // number, blanks last, using the domain comparison rather than the
    // repository's own name ordering.
    std::stable_sort(certificates.begin(), certificates.end(),
                     [](const Certificate& left, const Certificate& right) {
                         return CertificateListNumber::lessThan(left.listNumber,
                                                                right.listNumber);
                     });

    // settings-app-setting-spec §7, exactly as the list screen does it: a
    // failed read leaves the hardcoded defaults rather than failing the whole
    // document.
    AppSetting      setting;
    AlertThresholds thresholds;
    if (m_appSettings.read(&setting)) {
        thresholds.criticalDays     = setting.criticalDays;
        thresholds.expiringSoonDays = setting.expiringSoonDays;
        thresholds.dueSoonDays      = setting.dueSoonDays;
    }

    const QDate today = generatedAt.date();

    QString rows;
    for (const Certificate& certificate : certificates) {
        QList<Endorsement> endorsements;
        if (!m_endorsements.list(certificate.id, &endorsements)) {
            m_errorString = m_endorsements.errorString();
            return false;
        }

        std::optional<CertificateExtension> extension;
        if (!m_extensions.findActiveFor(certificate.id, &extension)) {
            m_errorString = m_extensions.errorString();
            return false;
        }
        const std::optional<QDate> extensionExpiry =
            extension.has_value() ? std::optional<QDate>(extension->expiryDate) : std::nullopt;

        const CertificateState state =
            computeCertificateState(certificate, endorsements, thresholds, today, extensionExpiry);

        rows += QStringLiteral("<tr>");
        rows += cell(certificate.listNumber);
        rows += cell(certificate.name);
        rows += cell(categoryLabel(certificate.category));
        rows += cell(certificate.certificateNumber);
        rows += cell(formatDate(certificate.issueDate));
        rows += cell(expiryText(certificate, state));
        // Blank unless a survey window is actually outstanding, the same as
        // the Survey From/To cells on screen.
        rows += cell(formatDate(state.windowOpens));
        rows += cell(formatDate(state.windowCloses));
        rows += cell(displayStatusLabel(state.display));
        rows += QStringLiteral("</tr>");
    }

    if (certificates.isEmpty()) {
        // A statement for a third party has to say this out loud: an empty
        // table could be read as a page that failed to print.
        rows = QStringLiteral("<tr><td colspan=\"9\">No certificates are recorded for this "
                              "vessel.</td></tr>");
    }

    const QString generatedLine =
        QStringLiteral("Generated %1").arg(generatedAt.toString(QStringLiteral("dd MMM yyyy HH:mm")));

    *html = QStringLiteral(
                "<html><head><style>"
                "body { font-family: sans-serif; font-size: 9pt; }"
                "h1 { font-size: 14pt; margin-bottom: 2px; }"
                "p.subtitle { color: #555555; margin-top: 0px; }"
                "table { width: 100%; border-collapse: collapse; }"
                "th, td { border: 1px solid #999999; padding: 3px; text-align: left; }"
                "th { background-color: #EEEEEE; }"
                "</style></head><body>"
                "<h1>%1</h1>"
                "<p class=\"subtitle\">IMO %2<br/>%3</p>"
                "<table><thead><tr>%4</tr></thead><tbody>%5</tbody></table>"
                "</body></html>")
                .arg(vessel->name.toHtmlEscaped(),
                     vessel->imoNumber.isEmpty() ? QStringLiteral("not set")
                                                 : vessel->imoNumber.toHtmlEscaped(),
                     generatedLine.toHtmlEscaped(),
                     headerCell(QStringLiteral("No.")) + headerCell(QStringLiteral("Name"))
                         + headerCell(QStringLiteral("Category"))
                         + headerCell(QStringLiteral("Certificate No."))
                         + headerCell(QStringLiteral("Issued"))
                         + headerCell(QStringLiteral("Expires"))
                         + headerCell(QStringLiteral("Survey From"))
                         + headerCell(QStringLiteral("Survey To"))
                         + headerCell(QStringLiteral("Status")),
                     rows);

    return true;
}

bool CertificateReportBuilder::exportToPdf(const QString& vesselId, const QString& filePath)
{
    // The one place the clock is read for this document, so the heading and
    // every computed status describe the same moment.
    QString html;
    if (!buildHtml(vesselId, QDateTime::currentDateTime(), &html)) {
        return false;
    }

    QTextDocument document;
    document.setHtml(html);

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    // Nine columns of dates do not fit across a portrait page without
    // wrapping every heading onto three lines.
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);

    document.print(&printer);

    // QPrinter reports nothing back, so the only honest check is whether a
    // file actually appeared.
    const QFileInfo written(filePath);
    if (!written.exists() || written.size() == 0) {
        m_errorString = QStringLiteral("The PDF could not be written to %1.").arg(filePath);
        return false;
    }

    return true;
}

QString CertificateReportBuilder::errorString() const
{
    return m_errorString;
}
