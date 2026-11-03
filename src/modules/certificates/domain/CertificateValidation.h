#pragma once

#include <QDate>
#include <QString>

struct Certificate;
struct Endorsement;

// "Can this be written?" for the two entities this module stores, as plain
// domain functions (CLAUDE.md §4 rule 1: the rules live in the domain layer,
// which knows nothing about widgets or SQL).
//
// Both were private members of their repositories until step 13. They had
// exactly one caller each — the repository's own create()/update() — and that
// was fine while the only way to enter a certificate was the edit dialog,
// which simply calls create()/update() and shows errorString() when a write
// is refused.
//
// CSV import broke that assumption. Its preview screen has to tell the user
// which rows *would* be refused before a single row is written
// (certificate-import-export-spec §5.5), so it needs to ask the question
// without attempting the write. Moving the rules here rather than copying
// them into the importer keeps one answer: the preview and the write path
// cannot drift apart, because they are the same function.
//
// Each returns true when the value may be written, and otherwise false with a
// user-facing sentence in `reason` — the same sentences the dialogs have
// always shown, moved verbatim.

bool validateCertificate(const Certificate& certificate, QString* reason);

// certificateIssueDate is the parent certificate's issue date, which the
// "a survey cannot predate the certificate" rule needs. An invalid date means
// "not known", and that one rule is then skipped.
bool validateEndorsement(const Endorsement& endorsement,
                         const QDate&       certificateIssueDate,
                         QString*           reason);
