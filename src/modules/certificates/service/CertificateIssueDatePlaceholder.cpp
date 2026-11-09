#include "modules/certificates/service/CertificateIssueDatePlaceholder.h"

#include "modules/certificates/service/CertificateCsvFormat.h"
#include "modules/certificates/service/CertificateCsvImporter.h"

QDate CertificateIssueDatePlaceholder::date()
{
    return QDate(1990, 1, 1);
}

QString CertificateIssueDatePlaceholder::note()
{
    return QStringLiteral("No real issue date was recorded for this item - 1990-01-01 is a "
                          "placeholder, not a fact.");
}

QString CertificateIssueDatePlaceholder::noteFor(const QString& existingNotes)
{
    if (existingNotes.trimmed().isEmpty()) {
        return note();
    }
    // Prepended, not appended: it is the first thing anyone opening the
    // certificate needs to know about it, and the row's own note keeps
    // whatever shape it had.
    return note() + QStringLiteral("\n\n") + existingNotes;
}

void applyIssueDatePlaceholder(CertificateImportPreview* preview, bool enabled)
{
    for (CertificateImportRow& row : preview->rows) {
        // A row assembled by hand rather than read from a file has no cells
        // to re-parse, so there is nothing this rule can say about it.
        if (row.cells.isEmpty()) {
            continue;
        }

        CertificateCsvRow parsed;
        QString           reason;
        const bool        valid = CertificateCsvFormat::parseRow(row.cells, preview->columnIndex,
                                                                 &parsed, &reason, enabled);

        row.parsed = parsed;

        if (!valid) {
            row.reason  = reason;
            row.outcome = ImportRowOutcome::Invalid;
            continue;
        }

        // The vessel is the selected one, never anything from the file
        // (certificate-import-export-spec §7) - re-applied here because
        // re-parsing has just rebuilt the certificate from the raw cells.
        row.parsed.certificate.vesselId = preview->vesselId;
        row.reason.clear();

        // Back to whatever this row's Id made it: new, a match, or new after
        // a deletion. Worked out once when the preview was built, so toggling
        // the checkbox never needs the database again.
        row.outcome = row.outcomeIfValid;
    }
}
