#include "modules/certificates/ui/StatusItem.h"

#include <QBrush>
#include <QColor>
#include <QCoreApplication>

namespace {

// certificate-list-status-spec §4. Pastel backgrounds with a darker
// foreground of the same hue: readable at table-row density, and they print
// without soaking the page.
struct Palette {
    const char* background;
    const char* foreground;
};

constexpr Palette kRed        = {"#F8D7DA", "#842029"};
constexpr Palette kOrange     = {"#FFE5CC", "#7A4100"};
constexpr Palette kYellow     = {"#FFF3CD", "#664D03"};
constexpr Palette kLightGreen = {"#E6F4EA", "#1E7A34"};

// Valid has no entry on purpose: it keeps the default row background, so an
// unremarkable certificate stays unremarkable.
const Palette* paletteFor(DisplayStatus status)
{
    switch (status) {
    case DisplayStatus::Expired:
    case DisplayStatus::SurveyOverdue:
    case DisplayStatus::Critical:
        return &kRed;
    case DisplayStatus::ExpiringSoon:
        return &kOrange;
    case DisplayStatus::SurveyDue:
        return &kYellow;
    case DisplayStatus::DueSoon:
        return &kLightGreen;
    case DisplayStatus::Valid:
        break;
    }
    return nullptr;
}

} // namespace

QString StatusItem::labelFor(DisplayStatus status)
{
    // QTableWidgetItem is not a QObject, so there is no tr() to call here.
    switch (status) {
    case DisplayStatus::Expired:
        return QCoreApplication::translate("StatusItem", "Expired");
    case DisplayStatus::SurveyOverdue:
        return QCoreApplication::translate("StatusItem", "Survey Overdue");
    case DisplayStatus::Critical:
        return QCoreApplication::translate("StatusItem", "Critical");
    case DisplayStatus::ExpiringSoon:
        return QCoreApplication::translate("StatusItem", "Expiring Soon");
    case DisplayStatus::SurveyDue:
        return QCoreApplication::translate("StatusItem", "Survey Due");
    case DisplayStatus::DueSoon:
        return QCoreApplication::translate("StatusItem", "Due Soon");
    case DisplayStatus::Valid:
        return QCoreApplication::translate("StatusItem", "Valid");
    }
    return QString();
}

StatusItem::StatusItem(DisplayStatus status)
    : QTableWidgetItem(labelFor(status))
{
    // DisplayStatus is declared worst-last, so the enum's own value is the
    // severity rank. That ordering is load-bearing here: reordering the enum
    // would silently reorder this column.
    setData(kSeverityRole, static_cast<int>(status));

    // Every label stays distinct as text even where three share a colour, so
    // the screen still reads in print and for anyone who cannot tell red from
    // orange from yellow (§4).
    if (const Palette* palette = paletteFor(status)) {
        setBackground(QBrush(QColor(QLatin1String(palette->background))));
        setForeground(QBrush(QColor(QLatin1String(palette->foreground))));
    }
}

bool StatusItem::operator<(const QTableWidgetItem& other) const
{
    return data(kSeverityRole).toInt() < other.data(kSeverityRole).toInt();
}
