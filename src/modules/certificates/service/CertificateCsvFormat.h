#pragma once

#include "modules/certificates/domain/Certificate.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// The certificate module's CSV column layout
// (certificate-import-export-spec §3), and the translation between one file
// row and one certificate.
//
// Pure: no database, no widgets, no filesystem. CsvFile (core) turns bytes
// into rows of text and back; this turns a row of text into a Certificate and
// back. Keeping the two apart is what lets the whole column list and every
// parsing rule be tested without a window or a database (§11).
struct CertificateCsvRow
{
    // The Id and Vessel IMO cells exactly as the file gave them. Neither is
    // part of the certificate: the id is only ever used to look for an
    // existing row to match (§5.1), and the IMO is only ever a sanity check
    // against the vessel already selected on screen (§7).
    QString fileId;
    QString vesselImoNumber;

    // Everything the file says about the certificate itself. vesselId is
    // deliberately left empty here — the file never chooses the vessel, the
    // importer fills it in from the selection (§7).
    Certificate certificate;

    // The dates from the two survey-list columns, in the order the file
    // listed them. Only populated for a track the certificate actually
    // requires; a list given for a track it does not require is ignored
    // rather than treated as an error (§9.9).
    QList<QDate> annualSurveys;
    QList<QDate> intermediateSurveys;
};

namespace CertificateCsvFormat {

// The header line export writes, in §3's order.
QStringList headerRow();

QStringList toRow(const Certificate&  certificate,
                  const QString&      vesselName,
                  const QString&      vesselImoNumber,
                  const QList<QDate>& annualSurveys,
                  const QList<QDate>& intermediateSurveys);

// Works out which position each known column sits at, matching header names
// case-insensitively and ignoring surrounding whitespace.
//
// Returns false with a message in `error` for a file-level refusal (§3): a
// required column missing, a header that matches nothing, or the same column
// named twice. A blank header cell is ignored — a trailing comma on the
// header line is a spreadsheet artefact, not a column.
bool mapHeader(const QStringList& headerCells, QHash<QString, int>* columnIndex, QString* error);

// Returns false with a message in `reason` when this row cannot be imported —
// everything §5.5 shows as Invalid. That includes the CSV-specific parsing
// rules (dates, booleans, category and mode codes, the semicolon lists) and
// then the shared validateCertificate() rules, so one call gives a complete
// verdict for one row.
//
// allowPlaceholderIssueDate is the opt-in from
// certificate-list-usability-fixes.md §3: with it set, a row whose Issue Date
// cell is *empty* gets the flagged placeholder date and note from
// CertificateIssueDatePlaceholder instead of being refused. A cell that holds
// something unreadable is still refused either way — that is a typo, and the
// whole point of §5.3 is not to guess at those. Off by default, so every
// existing caller is unchanged.
bool parseRow(const QStringList&         cells,
              const QHash<QString, int>& columnIndex,
              CertificateCsvRow*         row,
              QString*                   reason,
              bool                       allowPlaceholderIssueDate = false);

} // namespace CertificateCsvFormat
