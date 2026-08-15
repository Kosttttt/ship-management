#pragma once

#include "modules/certificates/domain/CertificateState.h"

#include <QTableWidgetItem>

// A table cell for the "Status" column: the label and background colour for a
// DisplayStatus, sorted by severity rather than alphabetically
// (certificate-list-status-spec §4).
//
// The same pattern as step 5's ListNumberItem, and for the same reason:
// QTableWidget sorts through QTableWidgetItem::operator<, which compares the
// displayed text. Alphabetically "Critical" would come before "Valid", which
// says nothing about how urgent either is.
class StatusItem : public QTableWidgetItem
{
public:
    // The severity rank travels in the item's data so the comparison works
    // against any item, not only another StatusItem.
    static constexpr int kSeverityRole = Qt::UserRole + 2;

    explicit StatusItem(DisplayStatus status);

    bool operator<(const QTableWidgetItem& other) const override;

    // The visible label for a status. Exposed so a test can name what it
    // expects to see rather than repeating the string.
    static QString labelFor(DisplayStatus status);
};
