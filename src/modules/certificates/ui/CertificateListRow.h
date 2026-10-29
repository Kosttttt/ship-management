#pragma once

#include <QObject>

// What one row of the certificate list carries besides its visible text.
//
// These live in a header of their own because the data is written in one place
// and read in another: CertificateListWidget::fillRow() sets them, and
// CertificateListActions reads them back to decide what the selection-driven
// buttons may do. Before the two were split apart they were private constants
// in the widget's own .cpp; a shared header is what stops the writer and the
// reader from drifting onto different numbers.
namespace CertificateRow {

// The certificate id, so an action knows which row it is acting on without a
// UUID ever being shown to the user.
constexpr int kCertificateIdRole = Qt::UserRole + 1;

// Whether that certificate never expires, so Renew and the extension button
// can rule themselves out without another database round trip
// (certificate-renewal-spec §5, certificate-extension-spec §7).
//
// Shares a numeric value with StatusItem::kSeverityRole, which is harmless:
// the two live on different cells of the row.
constexpr int kNeverExpiresRole = Qt::UserRole + 2;

// The active short-term extension's expiry date, or an invalid QDate when the
// certificate has none (certificate-extension-spec §7).
constexpr int kActiveExtensionRole = Qt::UserRole + 3;

// All three ride on the row's first cell, which is the one column guaranteed
// to exist on every row.
constexpr int kDataColumn = 0;

} // namespace CertificateRow
