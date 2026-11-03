#include "modules/certificates/domain/CertificateValidation.h"

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/Endorsement.h"

bool validateCertificate(const Certificate& certificate, QString* reason)
{
    if (certificate.name.trimmed().isEmpty()) {
        *reason = QStringLiteral("A certificate name is required.");
        return false;
    }
    if (certificate.category == CertificateCategory::Unset) {
        *reason = QStringLiteral("A category is required.");
        return false;
    }
    if (!certificate.issueDate.isValid()) {
        *reason = QStringLiteral("An issue date is required.");
        return false;
    }

    // The form's validator refuses the keystroke, so this only fires for a
    // value that arrived another way — a CSV import.
    if (!CertificateListNumber::isValid(certificate.listNumber.trimmed())) {
        *reason = QStringLiteral(
            "\"%1\" is not a valid number.\n\n"
            "A number is digits, optionally followed by letters — 15, 3A, 15D.")
                      .arg(certificate.listNumber);
        return false;
    }

    // The friendly version of the migration's CHECK constraint. The constraint
    // stays as the backstop; this is the path a user actually sees.
    if (certificate.neverExpires()
        && (certificate.requiresAnnualSurvey || certificate.requiresIntermediateSurvey)) {
        *reason = QStringLiteral(
            "A certificate with no expiry cannot require a survey.\n\n"
            "Surveys are scheduled against anniversaries of the expiry date, so there is "
            "nothing to schedule against.");
        return false;
    }

    return true;
}

bool validateEndorsement(const Endorsement& endorsement,
                         const QDate&       certificateIssueDate,
                         QString*           reason)
{
    if (endorsement.surveyType == SurveyType::Unset) {
        *reason = QStringLiteral("A survey type is required.");
        return false;
    }
    if (!endorsement.endorsementDate.isValid()) {
        *reason = QStringLiteral("An endorsement date is required.");
        return false;
    }

    // certificate-control-spec.md §4.8 item 5: a survey cannot have happened
    // before the certificate existed.
    if (certificateIssueDate.isValid() && endorsement.endorsementDate < certificateIssueDate) {
        *reason = QStringLiteral("The endorsement date is before the certificate was issued on %1.")
                      .arg(certificateIssueDate.toString(QStringLiteral("dd MMM yyyy")));
        return false;
    }

    return true;
}
