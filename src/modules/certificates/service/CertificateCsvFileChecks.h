#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct Vessel;

// The two refusals that are about the file as a whole rather than about one
// row of it (certificate-import-export-spec §7 and §9.6).
//
// Separate from CertificateCsvFormat, which answers "is this row importable",
// because these two answer "is this the right file at all" — and they run
// before any row is parsed, so that one bad row cannot hide the fact that the
// whole file is for another ship.
//
// Each returns true when the file passes, and otherwise false with a
// user-facing explanation in `error`. Neither writes anything.
namespace CertificateCsvFileChecks {

// §7: a Vessel IMO on any row that is not the selected vessel's own refuses
// the entire import. A blank cell is not checked — a hand-built file
// reasonably carries none. A file that does name an IMO while the selected
// vessel has none recorded is a mismatch, not a free pass (§9.5).
bool vesselMatches(const QList<QStringList>&  dataRows,
                   const QHash<QString, int>& columnIndex,
                   const Vessel&              vessel,
                   QString*                   error);

// §9.6: the same Id on two rows. Neither "first one wins" nor "last one wins"
// is a defensible guess about which of two conflicting versions of one
// certificate was meant.
bool idsAreUnique(const QList<QStringList>&  dataRows,
                  const QHash<QString, int>& columnIndex,
                  QString*                   error);

} // namespace CertificateCsvFileChecks
