#pragma once

#include <QDate>
#include <QTableWidgetItem>

// A table cell holding a calendar date: shown the way this screen shows dates,
// sorted the way a date sorts (certificate-attachment-addendum-spec §7 case 9).
//
// The same pattern as ListNumberItem and StatusItem, and for the same reason:
// QTableWidget sorts through QTableWidgetItem::operator<, which compares the
// displayed text. "dd MMM yyyy" compared as text sorts by the day of the month
// first, so 02 Dec 2026 would come before 11 Jan 2026. Storing the date itself
// alongside the label is what lets the comparison use the date.
//
// An invalid date shows nothing and sorts before every real date, which is
// what a blank cell should do.
class DateItem : public QTableWidgetItem
{
public:
    // The date travels in the item's data so the comparison works against any
    // item, not only another DateItem.
    static constexpr int kDateRole = Qt::UserRole + 4;

    // The format every date column on this screen uses. Held here so the
    // columns cannot drift apart.
    static const QString kDateFormat;

    explicit DateItem(const QDate& date);

    bool operator<(const QTableWidgetItem& other) const override;
};
