#include "modules/certificates/service/CertificateCsvValues.h"

#include <QStringList>

namespace {

const QString kDateFormat = QStringLiteral("yyyy-MM-dd");

} // namespace

bool CertificateCsvValues::parseDate(const QString& value, QDate* date)
{
    // The length check is what makes this strict rather than lenient: an
    // explicit format alone still accepts some shapes this must refuse.
    if (value.size() != 10) {
        return false;
    }
    *date = QDate::fromString(value, kDateFormat);
    return date->isValid();
}

QString CertificateCsvValues::formatDate(const QDate& date)
{
    return date.isValid() ? date.toString(kDateFormat) : QString();
}

bool CertificateCsvValues::parseBoolean(const QString& value, bool* result)
{
    const QString lowered = value.toLower();
    if (lowered.isEmpty() || lowered == QLatin1String("false") || lowered == QLatin1String("no")
        || lowered == QLatin1String("n") || lowered == QLatin1String("0")) {
        *result = false;
        return true;
    }
    if (lowered == QLatin1String("true") || lowered == QLatin1String("yes")
        || lowered == QLatin1String("y") || lowered == QLatin1String("1")) {
        *result = true;
        return true;
    }
    return false;
}

QString CertificateCsvValues::formatBoolean(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

bool CertificateCsvValues::parseDateList(const QString& value, QList<QDate>* dates,
                                         QString* badEntry)
{
    const QStringList parts = value.split(QLatin1Char(';'));
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QDate date;
        if (!parseDate(trimmed, &date)) {
            *badEntry = trimmed;
            return false;
        }
        dates->append(date);
    }
    return true;
}

QString CertificateCsvValues::formatDateList(const QList<QDate>& dates)
{
    QStringList parts;
    parts.reserve(dates.size());
    for (const QDate& date : dates) {
        parts.append(formatDate(date));
    }
    return parts.join(QLatin1Char(';'));
}
