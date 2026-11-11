#include "modules/certificates/ui/CertificateRowRenderer.h"

#include "modules/certificates/domain/CertificateState.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/CertificateListRow.h"
#include "modules/certificates/ui/DateItem.h"
#include "modules/certificates/ui/ListNumberItem.h"
#include "modules/certificates/ui/StatusItem.h"

#include <QTableWidget>

namespace {

// Shorter at every use below, and the only name this file needs for it.
using Column = CertificateColumn;

// The row data in CertificateListRow.h rides on the first cell, which is this
// column. Asserted rather than assumed, since the two are declared apart.
static_assert(static_cast<int>(Column::ListNumber) == CertificateRow::kDataColumn,
              "row data must live on the column fillRow() writes it to");

// The mark in the Document column (§4): a plain glyph when something is
// attached and an empty cell when nothing is, with no filename and no button.
//
// Written as an escape rather than as a literal paperclip so that this source
// file stays pure ASCII and cannot be damaged by a tool that re-saves it in
// another encoding. QStringLiteral builds UTF-16, where U+1F4CE is a surrogate
// pair, which is exactly what Qt wants.
const QString kAttachmentGlyph = QStringLiteral(u"\U0001F4CE");

QString expiryLabel(const Certificate& certificate, const CertificateState& state)
{
    // certificate-extension-spec §7: an active extension takes the cell over,
    // with a short suffix so it is obvious at a glance that the date shown is
    // not the certificate's own. No new column — every existing sort and
    // filter behaviour stays exactly as it was.
    if (state.activeExtensionExpiry.has_value()) {
        return CertificateRowRenderer::tr("%1 — Short Term")
            .arg(state.activeExtensionExpiry->toString(DateItem::kDateFormat));
    }
    if (certificate.neverExpires()) {
        return CertificateRowRenderer::tr("Does not expire");
    }
    return certificate.expiryDate.toString(DateItem::kDateFormat);
}

} // namespace

CertificateRowRenderer::CertificateRowRenderer(QTableWidget&                 table,
                                               CertificateAttachmentService& attachments,
                                               QObject*                      parent)
    : QObject(parent)
    , m_table(table)
    , m_attachments(attachments)
{
}

void CertificateRowRenderer::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
}

void CertificateRowRenderer::fillRow(int                     row,
                                     const Certificate&      certificate,
                                     const CertificateState& state)
{
    // The id rides on the first cell, so an edit knows which row it is
    // editing without a UUID ever being shown.
    auto* numberItem = new ListNumberItem(certificate.listNumber);
    numberItem->setData(CertificateRow::kCertificateIdRole, certificate.id);
    numberItem->setData(CertificateRow::kNeverExpiresRole, certificate.neverExpires());
    // An invalid QDate when there is no extension, which is what both buttons
    // test for (certificate-extension-spec §7).
    numberItem->setData(CertificateRow::kActiveExtensionRole,
                        state.activeExtensionExpiry.has_value()
                            ? *state.activeExtensionExpiry
                            : QDate());

    m_table.setItem(row, static_cast<int>(Column::ListNumber), numberItem);
    m_table.setItem(row, static_cast<int>(Column::Name), new QTableWidgetItem(certificate.name));

    // §5: a plain field with nothing computed about it, so there is no state to
    // consult — only the certificate's own issue date. A DateItem rather than
    // formatted text, so the column sorts chronologically (§7 case 9).
    m_table.setItem(row, static_cast<int>(Column::IssueDate),
                    new DateItem(certificate.issueDate));

    m_table.setItem(row, static_cast<int>(Column::Status), new StatusItem(state.display));
    m_table.setItem(row, static_cast<int>(Column::Expiry),
                    new QTableWidgetItem(expiryLabel(certificate, state)));

    // certificate-list-status-spec §5: blank unless there is an outstanding
    // survey window to show. A DateItem renders an invalid date as a blank
    // cell, so "is there a window" needs no separate test here — and these two
    // now sort chronologically alongside Issue Date rather than as text.
    m_table.setItem(row, static_cast<int>(Column::SurveyFrom), new DateItem(state.windowOpens));
    m_table.setItem(row, static_cast<int>(Column::SurveyTo), new DateItem(state.windowCloses));

    // §6: the raw signed number, negative once overdue. A certificate that
    // never expires has no meaningful count, and a bare 0 would read as "due
    // today", so that cell is left blank instead.
    auto* daysItem = new QTableWidgetItem;
    // An active extension supplies a date to count to even for a certificate
    // that has none of its own, so the cell is driven by whether the state
    // actually counted to something rather than by the certificate alone.
    if (certificate.expiryDate.isValid() || state.activeExtensionExpiry.has_value()) {
        // Stored as a number, not text, so the column sorts numerically
        // rather than putting "-7" after "10".
        daysItem->setData(Qt::DisplayRole, state.daysLeft);
    }
    m_table.setItem(row, static_cast<int>(Column::DaysLeft), daysItem);

    // §4: one more read per certificate — the same accepted N+1 the
    // endorsements and the active extension above already cost on this screen.
    // Asked here rather than remembered on the row, so that what the cell shows
    // and what a click opens are the same question answered by the same
    // service.
    const bool hasAttachment =
        !m_attachments.currentAbsolutePath(m_vesselId, certificate.id).isEmpty();

    auto* attachmentItem = new QTableWidgetItem(hasAttachment ? kAttachmentGlyph : QString());
    attachmentItem->setTextAlignment(Qt::AlignCenter);
    if (hasAttachment) {
        attachmentItem->setToolTip(tr("Click to open the attached document."));
    }
    m_table.setItem(row, static_cast<int>(Column::Attachment), attachmentItem);
}
