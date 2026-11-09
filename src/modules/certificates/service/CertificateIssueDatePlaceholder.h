#pragma once

#include <QDate>
#include <QString>

struct CertificateImportPreview;

// The opt-in rule that lets a CSV row with no issue date at all be imported
// anyway, under a flagged placeholder
// (certificate-list-usability-fixes.md §3).
//
// Why it exists: the first real bulk import was a hand-built sheet that had
// several rows with no issue date — mostly items that are not certificates at
// all (a planned-maintenance reminder, a spare part) but which the fleet
// still wants tracked against an expiry date. Those rows are Invalid by
// default and stay that way, because a missing issue date is just as often a
// data-entry slip worth catching. This is the explicit "yes, I know" for a
// whole file at once, instead of editing the CSV by hand.
//
// Why it lives here and not in the dialog: "a missing issue date can be
// patched with a flagged placeholder" is a rule about what the data means,
// not about how a dialog looks (CLAUDE.md §4 rule 3). The dialog owns the
// checkbox and nothing else — it does not get to decide what the date is or
// what the note says.
namespace CertificateIssueDatePlaceholder {

// 1990-01-01: old enough that it can never be mistaken for a real issue date
// on a certificate in service, and early enough that it never pushes a survey
// date below it (an endorsement may not predate the issue date).
QDate date();

// The sentence written into the certificate's Notes. This is the only thing
// that will ever tell anyone, years later, that the date is not a fact — so
// it travels with the row into the database rather than living in a log.
QString note();

// note() prepended to whatever the row already had in Notes, with the row's
// own text kept intact below it.
QString noteFor(const QString& existingNotes);

} // namespace CertificateIssueDatePlaceholder

// Re-evaluates every row of an already-open preview with the placeholder rule
// switched on or off, in place.
//
// This is what lets the checkbox take effect in the table the user is already
// looking at, without re-picking the file (§3). It re-parses each row from the
// raw cells the preview kept, so a row rescued by the placeholder is parsed
// exactly as it would have been had the box been ticked from the start —
// including the cross-field expiry rule and the survey dates, which are
// checked against the placeholder date like any other issue date.
//
// Only a row whose *sole* problem was the missing issue date changes outcome.
// A row with a malformed issue date stays Invalid, because its cell was not
// empty: it is a typo, which is the case worth catching. A row with any other
// problem stays Invalid too, because re-parsing hits that problem again.
//
// Each row's matched/new classification, and the user's own overwrite ticks,
// are left exactly as they were.
void applyIssueDatePlaceholder(CertificateImportPreview* preview, bool enabled);
