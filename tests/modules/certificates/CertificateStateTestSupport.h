#pragma once

#include "modules/certificates/domain/CertificateState.h"

// Shared builders for the computeCertificateState() suites. No database and
// no widgets — the whole point of §4 being a pure function.
namespace CertificateStateTestSupport {

// A five-year certificate expiring on the given date, requiring an annual
// survey. Anniversaries therefore fall on expiry minus 4, 3, 2 and 1 years.
inline Certificate certificateExpiring(const QDate& expiry, int termYears = 5)
{
    Certificate certificate;
    certificate.id                   = QStringLiteral("cert-1");
    certificate.vesselId             = QStringLiteral("vessel-1");
    certificate.name                 = QStringLiteral("Cargo Ship Safety Construction");
    certificate.category             = CertificateCategory::Statutory;
    certificate.issueDate            = expiry.addYears(-termYears);
    certificate.expiryDate           = expiry;
    certificate.requiresAnnualSurvey = true;
    return certificate;
}

inline Endorsement endorsementOn(const QDate& date, SurveyType type)
{
    Endorsement endorsement;
    endorsement.id              = QStringLiteral("end-%1").arg(date.toString(Qt::ISODate));
    endorsement.certificateId   = QStringLiteral("cert-1");
    endorsement.surveyType      = type;
    endorsement.endorsementDate = date;
    return endorsement;
}

// The nth anniversary of a certificate, 1-based, as §4.5 numbers them.
inline QDate anniversary(const Certificate& certificate, int n)
{
    const int termYears = certificate.expiryDate.year() - certificate.issueDate.year();
    return certificate.expiryDate.addYears(-(termYears - n));
}

inline QDate windowOpens(const QDate& anniversaryDate)
{
    return anniversaryDate.addMonths(-3);
}

inline QDate windowCloses(const QDate& anniversaryDate)
{
    return anniversaryDate.addMonths(3);
}

} // namespace CertificateStateTestSupport
