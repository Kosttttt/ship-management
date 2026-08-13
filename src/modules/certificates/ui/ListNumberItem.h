#pragma once

#include <QTableWidgetItem>

// A table cell for the "No." column that sorts by what the number means
// rather than by its characters (certificate-crud-spec §8.2).
//
// QTableWidget sorts through QTableWidgetItem::operator<, which compares the
// displayed text. That would put "15D" ahead of "3A", because '1' precedes
// '3' as a character. This subclass exists solely to hand that comparison to
// the domain rule instead.
class ListNumberItem : public QTableWidgetItem
{
public:
    explicit ListNumberItem(const QString& listNumber);

    bool operator<(const QTableWidgetItem& other) const override;
};
