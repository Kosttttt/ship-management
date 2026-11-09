#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

class QTableWidget;
class QTableWidgetItem;

// Reading the certificate table's selection, in one place.
//
// The table became multi-select in
// certificate-list-usability-fixes.md §2, and that turned "what is
// selected?" into a question with a rule attached rather than an obvious
// lookup. The rule:
//
//   Every action except Delete is inherently about one certificate - renewing
//   it, extending it, attaching a document to it. For those, "more than one
//   row selected" is not a selection at all, exactly as "no rows selected"
//   is not. Answering -1 / empty / invalid for both is what disables Renew,
//   the short-term extension button and the Attachment button the moment a
//   second row is picked, with no extra logic anywhere.
//
// Keeping it here rather than in each action class is the point: before this,
// CertificateListActions and CertificateAttachmentActions each had their own
// copy of "the first selected row", and only one of them would have learned
// the new rule.
//
// Free functions rather than a class: there is no state, only a table to ask.
namespace CertificateSelection {

// The row of the single selected certificate, or -1 when none - or more than
// one - is selected.
int singleRow(const QTableWidget* table);

// The first cell of that row, which is where CertificateListRow hangs the row
// data. Null on the same terms singleRow() returns -1 on.
QTableWidgetItem* singleDataItem(const QTableWidget* table);

QString singleCertificateId(const QTableWidget* table);

// The active short-term extension's expiry on that row, or an invalid date
// when there is none - or no single selection.
QDate singleExtensionExpiry(const QTableWidget* table);

// Every selected row's certificate id, in the order the rows appear. Empty
// when nothing is selected. This is the one accessor that does not apply the
// rule above, because Delete is the one action that genuinely works on a
// whole selection (§2).
QStringList allCertificateIds(const QTableWidget* table);

} // namespace CertificateSelection
