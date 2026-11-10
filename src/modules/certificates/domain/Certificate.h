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

// The rules for the "No." field — the company reference number
// (certificate-crud-spec §8.2, widened by
// certificate-list-number-prefix-spec.md).
//
// They live in the domain because four places need exactly the same rule and
// must not drift: the form's validator, the repository's check before
// writing, the CSV importer, and the list's sort order.
namespace CertificateListNumber {

// An optional short letter prefix, then digits, then optional trailing
// letters — "15", "3A", "S1", "P.6", "USCA.4" — or nothing at all.
// prefix-spec §2 has the format and why it is this shape; this is the single
// definition every other layer is built from.
//
// Handed to the form's QLineEdit validator so an invalid character never
// appears on screen in the first place.
QString pattern();

// Blank counts as valid: the field is optional.
bool isValid(const QString& value);

// Orders by prefix group first, then by number, then by trailing letters —
// so the plain numbers come first, then each prefixed annex in the order the
// company's own S-14 sheet lists them. prefix-spec §3 has the group priority
// list, the fallback for a prefix not on it, and the worked example this is
// tested against. Plain text comparison gets all of this wrong.
//
// A blank number sorts after every certificate that has one, keeping the ones
// the fleet refers to by number together at the top.
bool lessThan(const QString& left, const QString& right);

} // namespace CertificateListNumber
