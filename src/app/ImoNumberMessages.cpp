#include "app/ImoNumberMessages.h"

#include <QCoreApplication>

QString ImoNumberMessages::describe(ImoNumberValidator::Result result)
{
    switch (result) {
    case ImoNumberValidator::Result::Valid:
        return QString();
    case ImoNumberValidator::Result::Empty:
        return QCoreApplication::translate("ImoNumberMessages", "An IMO number is required.");
    case ImoNumberValidator::Result::WrongLength:
        return QCoreApplication::translate("ImoNumberMessages",
                                           "An IMO number has seven digits.");
    case ImoNumberValidator::Result::CheckDigitMismatch:
        return QCoreApplication::translate(
            "ImoNumberMessages",
            "The check digit does not match. Please re-read the number from the certificate.");
    }
    return QString();
}
