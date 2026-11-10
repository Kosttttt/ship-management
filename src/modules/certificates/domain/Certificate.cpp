#include "modules/certificates/domain/Certificate.h"

#include <QRegularExpression>
#include <QStringList>

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

// One regex, three captures: the optional prefix, the digit run, the optional
// trailing letters. The old hand-rolled "count the leading digits" loop could
// not survive prefix-spec §2, which no longer lets position 0 be assumed to
// be a digit.
const QRegularExpression& splitExpression()
{
    static const QRegularExpression expression(QRegularExpression::anchoredPattern(
        QStringLiteral("([A-Za-z]{1,6}[.]?)?([0-9]+)([A-Za-z]*)")));
    return expression;
}

// Splits "USCA.4" into ("USCA", 4, ""), "15D" into ("", 15, "D"), and "S1"
// into ("S", 1, "").
//
// The prefix comes back normalised for comparison — uppercased and with any
// trailing dot removed — so "p.6" and "P.6" are one certificate, exactly the
// reasoning already applied to "3a" and "3A". The sheet's own spelling is
// what gets stored and displayed; only the comparison normalises.
//
// A value the expression cannot match at all is handled rather than
// asserted: the database's own CHECK is deliberately looser than
// isValid() (prefix-spec §4), so a row written past the application could in
// principle hold something like "A1B2". Such a value is treated as a prefix
// of its whole self, which lands it in the unknown-prefix group and keeps
// this a strict weak ordering instead of an undefined one.
void splitListNumber(const QString& value, QString* prefix, qlonglong* number,
                     QString* letters)
{
    const QRegularExpressionMatch match = splitExpression().match(value);
    if (!match.hasMatch()) {
        *prefix  = value.toUpper();
        *number  = 0;
        *letters = QString();
        return;
    }

    QString rawPrefix = match.captured(1);
    if (rawPrefix.endsWith(QLatin1Char('.'))) {
        rawPrefix.chop(1);
    }

    *prefix  = rawPrefix.toUpper();
    *number  = match.captured(2).toLongLong();
    *letters = match.captured(3);
}

// prefix-spec §3: the company's S-14 sheet's own section order, which is what
// the list groups by. Not alphabetical, and deliberately a fixed list — the
// trade-off being that a ninth annex has no defined position until it is
// added here, which is the one place that spec needs revisiting.
const QStringList& groupOrder()
{
    static const QStringList order = {
        QString(), // the plain numbers: Statutory / Class Surveys / Equipment
        QStringLiteral("S"),    // Statements
        QStringLiteral("P"),    // Plans and Documents
        QStringLiteral("EU"),   // Compliance with EU Directives
        QStringLiteral("US"),   // USA - Federal
        QStringLiteral("USCA"), // USA - California
        QStringLiteral("USAL"), // USA - Alaska
        QStringLiteral("PC"),   // Panama Canal
        QStringLiteral("IS"),   // Israel
    };
    return order;
}

// A prefix the sheet does not have yet sorts after every known group rather
// than being lost or rejected (§3). Every unknown prefix shares this one
// rank, so lessThan() orders them alphabetically between themselves.
int groupRankOf(const QString& prefix)
{
    const int index = groupOrder().indexOf(prefix);
    return index >= 0 ? index : groupOrder().size();
}

} // namespace

QString CertificateListNumber::pattern()
{
    // prefix-spec §2. Shared verbatim with the form's validator, so widening
    // it here widens what can be typed as well as what can be saved.
    return QStringLiteral("(?:(?:[A-Za-z]{1,6}[.]?)?[0-9]+[A-Za-z]*)?");
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

    QString   leftPrefix;
    QString   rightPrefix;
    qlonglong leftNumber = 0;
    qlonglong rightNumber = 0;
    QString   leftLetters;
    QString   rightLetters;
    splitListNumber(left, &leftPrefix, &leftNumber, &leftLetters);
    splitListNumber(right, &rightPrefix, &rightNumber, &rightLetters);

    // §3 step 2: the annex a certificate belongs to outranks its number, so
    // every S-number sits together, after all the plain numbers and before
    // every P-number.
    const int leftRank  = groupRankOf(leftPrefix);
    const int rightRank = groupRankOf(rightPrefix);
    if (leftRank != rightRank) {
        return leftRank < rightRank;
    }

    // Two prefixes the sheet does not know share one rank, so they need a
    // tiebreak of their own or the order between them would be arbitrary.
    if (leftPrefix != rightPrefix) {
        return leftPrefix < rightPrefix;
    }

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
