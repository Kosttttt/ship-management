#pragma once

#include "modules/certificates/service/CertificateCsvFormat.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <QString>

class CertificateRepository;
class EndorsementRepository;
class QSqlDatabase;
class VesselRepository;

// What the preview screen says about one row
// (certificate-import-export-spec §5.5). Every row ends up in exactly one of
// these; there is deliberately no fourth "excluded" state.
enum class ImportRowOutcome {
    New,
    // §5.1: new, but its Id names a certificate this vessel once had and
    // deleted. Shown apart from plain New so that the resurrected certificate
    // getting a different Id than the file's does not read as a bug.
    NewPreviouslyDeleted,
    Match,
    Invalid
};

struct CertificateImportRow
{
    // Which data row of the file this is, counting from 1 and not counting
    // the header. Not a file line number: a quoted value may span several
    // lines, and a blank line is no row at all.
    int rowNumber = 0;

    ImportRowOutcome outcome = ImportRowOutcome::Invalid;

    // Invalid only: the one specific sentence saying what is wrong.
    QString reason;

    // What the file said. certificate.vesselId is filled in by the importer
    // from the selected vessel, never from the file (§7).
    CertificateCsvRow parsed;

    // Match only: the existing certificate's own id.
    QString matchedId;

    // Match only: the user's per-row choice. False — skip — is the default
    // that §5.5 requires, so an unattended import can never overwrite.
    bool overwrite = false;

    // The row exactly as the file held it, kept so the preview can be
    // re-evaluated without re-reading the file
    // (certificate-list-usability-fixes.md §3 - the placeholder checkbox).
    // Empty on a row assembled by hand rather than parsed, which is then left
    // alone by any re-evaluation.
    QStringList cells;

    // What this row's Id makes it when it parses: New, Match or New
    // (previously deleted). Never Invalid. Worked out once, when the preview
    // was built and the vessel's live and deleted id sets were in hand, so
    // that toggling the placeholder option can put a rescued row back into
    // its real outcome without going near the database again.
    ImportRowOutcome outcomeIfValid = ImportRowOutcome::New;
};

struct CertificateImportPreview
{
    QList<CertificateImportRow> rows;

    // Which vessel the rows were previewed against — the one selected on
    // screen, never anything from the file (§7). Kept so a re-evaluation can
    // put it back on a row it has just rebuilt from the raw cells.
    QString vesselId;

    // Where each known column sits in `cells`, from mapHeader(). Kept for the
    // same reason as the cells themselves.
    QHash<QString, int> columnIndex;

    // §5.5: the whole import is blocked while any row is Invalid.
    bool hasInvalidRows() const;
    int  countOf(ImportRowOutcome outcome) const;
};

// Reads a CSV file into a preview, and then writes the preview
// (certificate-import-export-spec §5).
//
// Composes two repositories like every other service in this module, and
// additionally holds a QSqlDatabase& for one purpose only: the transaction
// that wraps the whole row loop, so an import either fully succeeds or
// changes nothing (§5.2). It issues no SQL of its own — every write goes
// through CertificateRepository::create()/update() and
// EndorsementRepository::create(), which is what makes CSV import subject to
// exactly the same validation, scope enforcement and audit columns as the
// edit dialog (CLAUDE.md §4 rule 2).
class CertificateCsvImporter
{
public:
    CertificateCsvImporter(QSqlDatabase&          database,
                           CertificateRepository& certificates,
                           EndorsementRepository& endorsements,
                           VesselRepository&      vessels);

    // Parses and classifies every row, writing nothing. Returns false for a
    // file-level refusal — a missing or unrecognised header, a duplicated Id,
    // a Vessel IMO that is not this vessel's — with the reason in
    // errorString(). An Invalid *row* is not a refusal: it comes back inside
    // the preview so the user can see which row to fix.
    bool buildPreview(const QString&            vesselId,
                      const QString&            filePath,
                      CertificateImportPreview* preview);

    // Writes the preview. Refuses outright if any row is still Invalid.
    // Either everything happens or nothing does.
    bool runImport(const QString&                  vesselId,
                   const CertificateImportPreview& preview,
                   int*                            createdCount = nullptr,
                   int*                            updatedCount = nullptr);

    QString errorString() const;

private:
    // One new certificate plus the endorsements its survey-list columns ask
    // for. Inside the caller's transaction, so a failure half-way leaves
    // nothing behind.
    bool createRow(const CertificateImportRow& row);
    bool updateRow(const CertificateImportRow& row);

    QSqlDatabase&          m_database;
    CertificateRepository& m_certificates;
    EndorsementRepository& m_endorsements;
    VesselRepository&      m_vessels;
    QString                m_errorString;
};
