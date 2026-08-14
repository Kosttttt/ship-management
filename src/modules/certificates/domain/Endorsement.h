#pragma once

#include <QDate>
#include <QString>

// The domain layer: plain C++ with no widgets and no SQL (CLAUDE.md §4 rule 1).

// Which kind of survey an endorsement records
// (certificate-control-spec.md §3.4).
//
// Only Annual and Intermediate are matched against a survey window by
// computeCertificateState(). Initial and Renewal are valid stored values that
// a later step may write — they are kept for the historical record and
// satisfy nothing.
enum class SurveyType {
    // "Not chosen yet", so the form can start with nothing selected and the
    // repository can reject it. Never stored — the same role
    // CertificateCategory::Unset already plays.
    Unset,
    Initial,
    Annual,
    Intermediate,
    Renewal
};

struct Endorsement {
    QString    id;
    QString    certificateId;
    SurveyType surveyType = SurveyType::Unset;

    // Required. A calendar date, never an instant.
    QDate endorsementDate;

    QString place;
    QString surveyor;
    QString result;
    QString remarks;
};

// The canonical stored codes, alongside CertificateCodes for the same reason:
// they are the enum's own names, and the CSV import in a later step will need
// exactly the same mapping.
namespace SurveyTypeCodes {

QString    toCode(SurveyType type);
SurveyType fromCode(const QString& code);

} // namespace SurveyTypeCodes
