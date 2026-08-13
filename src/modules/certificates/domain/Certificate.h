#pragma once

#include <QDate>
#include <QString>

// The domain layer: plain C++ with no widgets and no SQL (CLAUDE.md §4 rule 1).
// QDate and QString are permitted — hand-rolling date arithmetic would be worse.

// A broad bucket for filtering and reporting, not authoritative data
// (certificate-control-spec §5).
enum class CertificateCategory {
    // "Not chosen yet" has to be representable so the form can start with
    // nothing selected and the repository can reject it. It is never stored:
    // create()/update() refuse it before any write, and the migration's CHECK
    // constraint only permits the four real codes.
    Unset,
    Statutory,
    Class,
    Equipment,
    Other
};

// Whether an intermediate survey adds to the annual surveys or replaces one
// (certificate-control-spec §3.4). Set per certificate, never looked up.
enum class IntermediateMode {
    Additional,
    ReplacesAnnual
};

struct Certificate {
    QString             id;
    QString             vesselId;

    // The company's own short reference — "15D" — used when talking about a
    // certificate rather than typing its full name. Optional
    // (certificate-crud-spec §8.2).
    QString             listNumber;

    QString             name;
    CertificateCategory category = CertificateCategory::Unset;
    QString             certificateNumber;
    QString             appliesTo;

    QDate issueDate;
    // A null QDate means "does not expire" (certificate-control-spec §4).
    QDate expiryDate;

    QString issuedBy;
    QString placeOfIssue;
    bool    isInterim = false;

    bool             requiresAnnualSurvey       = false;
    bool             requiresIntermediateSurvey = false;
    IntermediateMode intermediateMode           = IntermediateMode::Additional;

    QString notes;

    // True when this certificate never expires.
    bool neverExpires() const { return !expiryDate.isValid(); }
};

// The canonical stored codes. They live in the domain rather than in the
// repository because they are the enum's own names, not a storage detail —
// the CSV import in a later step needs exactly the same mapping.
namespace CertificateCodes {

QString             categoryToCode(CertificateCategory category);
CertificateCategory categoryFromCode(const QString& code);

QString          intermediateModeToCode(IntermediateMode mode);
IntermediateMode intermediateModeFromCode(const QString& code);

} // namespace CertificateCodes

// The rules for the "No." field (certificate-crud-spec §8.2). They live in the
// domain because three layers need exactly the same rule and must not drift:
// the form's validator, the repository's check before writing, and the list's
// sort order.
namespace CertificateListNumber {

// "one or more digits, followed by zero or more letters" — or nothing at all.
// Handed to the form's QLineEdit validator so an invalid character never
// appears on screen in the first place.
QString pattern();

// Blank counts as valid: the field is optional.
bool isValid(const QString& value);

// Orders by the leading number first, then the trailing letters, so "3" <
// "3A" < "3B" < "9" < "15D". Plain text comparison gets this backwards.
// A blank number sorts after every certificate that has one, keeping the ones
// the fleet refers to by number together at the top.
bool lessThan(const QString& left, const QString& right);

} // namespace CertificateListNumber
