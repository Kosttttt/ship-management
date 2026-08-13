#include "modules/certificates/domain/Certificate.h"

#include <QRegularExpression>

QString CertificateCodes::categoryToCode(CertificateCategory category)
{
    switch (category) {
    case CertificateCategory::Statutory:
        return QStringLiteral("STATUTORY");
    case CertificateCategory::Class:
        return QStringLiteral("CLASS");
    case CertificateCategory::Equipment:
        return QStringLiteral("EQUIPMENT");
    case CertificateCategory::Other:
        return QStringLiteral("OTHER");
    case CertificateCategory::Unset:
        break;
    }
    // Unset has no code on purpose: it must never reach the database.
    return QString();
}

CertificateCategory CertificateCodes::categoryFromCode(const QString& code)
{
    if (code == QLatin1String("STATUTORY")) {
        return CertificateCategory::Statutory;
    }
    if (code == QLatin1String("CLASS")) {
        return CertificateCategory::Class;
    }
    if (code == QLatin1String("EQUIPMENT")) {
        return CertificateCategory::Equipment;
    }
    if (code == QLatin1String("OTHER")) {
        return CertificateCategory::Other;
    }
    return CertificateCategory::Unset;
}

QString CertificateCodes::intermediateModeToCode(IntermediateMode mode)
{
    return (mode == IntermediateMode::ReplacesAnnual) ? QStringLiteral("REPLACES_ANNUAL")
                                                      : QStringLiteral("ADDITIONAL");
}

IntermediateMode CertificateCodes::intermediateModeFromCode(const QString& code)
{
    return (code == QLatin1String("REPLACES_ANNUAL")) ? IntermediateMode::ReplacesAnnual
                                                      : IntermediateMode::Additional;
}

namespace {

// Splits "15D" into 15 and "D". The format guarantees this split is
// unambiguous, which is the whole reason for the format.
void splitListNumber(const QString& value, qlonglong* number, QString* letters)
{
    int digitCount = 0;
    while (digitCount < value.size() && value.at(digitCount).isDigit()) {
        ++digitCount;
    }
    *number  = value.left(digitCount).toLongLong();
    *letters = value.mid(digitCount);
}

} // namespace

QString CertificateListNumber::pattern()
{
    return QStringLiteral("(?:[0-9]+[A-Za-z]*)?");
}

bool CertificateListNumber::isValid(const QString& value)
{
    if (value.isEmpty()) {
        return true; // the field is optional
    }
    static const QRegularExpression expression(
        QRegularExpression::anchoredPattern(CertificateListNumber::pattern()));
    return expression.match(value).hasMatch();
}

bool CertificateListNumber::lessThan(const QString& left, const QString& right)
{
    const bool leftBlank  = left.isEmpty();
    const bool rightBlank = right.isEmpty();

    if (leftBlank != rightBlank) {
        return rightBlank; // a blank number sorts after one that is set
    }
    if (leftBlank) {
        return false; // both blank: neither comes first
    }

    qlonglong leftNumber  = 0;
    qlonglong rightNumber = 0;
    QString   leftLetters;
    QString   rightLetters;
    splitListNumber(left, &leftNumber, &leftLetters);
    splitListNumber(right, &rightNumber, &rightLetters);

    if (leftNumber != rightNumber) {
        return leftNumber < rightNumber;
    }

    // "3a" and "3A" are the same certificate to a human, so compare without
    // case first and only fall back to case for a stable order.
    const int insensitive = QString::compare(leftLetters, rightLetters, Qt::CaseInsensitive);
    if (insensitive != 0) {
        return insensitive < 0;
    }
    return QString::compare(leftLetters, rightLetters, Qt::CaseSensitive) < 0;
}
