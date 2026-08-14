#include "modules/certificates/domain/Endorsement.h"

QString SurveyTypeCodes::toCode(SurveyType type)
{
    switch (type) {
    case SurveyType::Initial:
        return QStringLiteral("INITIAL");
    case SurveyType::Annual:
        return QStringLiteral("ANNUAL");
    case SurveyType::Intermediate:
        return QStringLiteral("INTERMEDIATE");
    case SurveyType::Renewal:
        return QStringLiteral("RENEWAL");
    case SurveyType::Unset:
        break;
    }
    // Unset has no code on purpose: it must never reach the database.
    return QString();
}

SurveyType SurveyTypeCodes::fromCode(const QString& code)
{
    if (code == QLatin1String("INITIAL")) {
        return SurveyType::Initial;
    }
    if (code == QLatin1String("ANNUAL")) {
        return SurveyType::Annual;
    }
    if (code == QLatin1String("INTERMEDIATE")) {
        return SurveyType::Intermediate;
    }
    if (code == QLatin1String("RENEWAL")) {
        return SurveyType::Renewal;
    }
    return SurveyType::Unset;
}
