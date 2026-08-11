#include "core/ImoNumberValidator.h"

namespace {

constexpr int kImoDigitCount = 7;

} // namespace

QString ImoNumberValidator::digitsOnly(const QString& input)
{
    QString digits;
    digits.reserve(input.size());
    for (const QChar ch : input) {
        if (ch.isDigit()) {
            digits.append(ch);
        }
    }
    return digits;
}

ImoNumberValidator::Result ImoNumberValidator::validate(const QString& input)
{
    const QString digits = digitsOnly(input);

    if (digits.isEmpty()) {
        return Result::Empty;
    }
    if (digits.size() != kImoDigitCount) {
        return Result::WrongLength;
    }

    // first-run-wizard-spec §3:
    //   (7*d1 + 6*d2 + 5*d3 + 4*d4 + 3*d5 + 2*d6) mod 10 == d7
    // The weights run 7 down to 2 across the first six digits.
    int sum = 0;
    for (int position = 0; position < kImoDigitCount - 1; ++position) {
        const int weight = kImoDigitCount - position;
        sum += weight * digits.at(position).digitValue();
    }

    const int expected = sum % 10;
    const int actual   = digits.at(kImoDigitCount - 1).digitValue();

    return (expected == actual) ? Result::Valid : Result::CheckDigitMismatch;
}

bool ImoNumberValidator::isValid(const QString& input)
{
    return validate(input) == Result::Valid;
}
