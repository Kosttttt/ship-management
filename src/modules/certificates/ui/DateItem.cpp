#include "modules/certificates/ui/DateItem.h"

const QString DateItem::kDateFormat = QStringLiteral("dd MMM yyyy");

DateItem::DateItem(const QDate& date)
    : QTableWidgetItem(date.isValid() ? date.toString(kDateFormat) : QString())
{
    setData(kDateRole, date);
}

bool DateItem::operator<(const QTableWidgetItem& other) const
{
    const QDate mine  = data(kDateRole).toDate();
    const QDate their = other.data(kDateRole).toDate();

    // An invalid date sorts first, so blanks gather at one end instead of
    // interleaving unpredictably. QDate's own operator< treats two invalid
    // dates as equal, which is the right answer for two blank cells.
    if (!mine.isValid() || !their.isValid()) {
        return their.isValid();
    }
    return mine < their;
}
