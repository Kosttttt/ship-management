#pragma once

#include "core/InstallationContext.h"
#include "modules/certificates/domain/Certificate.h"

#include <QList>
#include <QSet>
#include <QString>

#include <optional>

class QSqlDatabase;

// The only file in this module containing SQL (CLAUDE.md §4 rule 2).
//
// Mirrors VesselRepository closely: the VESSEL-mode scope filter is applied
// here on every query, never left to a screen (CLAUDE.md §3).
class CertificateRepository
{
public:
    CertificateRepository(QSqlDatabase& database, const InstallationContext& installation);

    // Reads apply is_deleted = 0 and, in VESSEL mode, silently intersect the
    // requested vessel with the installation's own. Asking for another
    // vessel's data returns nothing rather than an error — "not found" is a
    // successful answer.
    bool list(const QString& vesselId, QList<Certificate>* certificates);
    bool findById(const QString& id, std::optional<Certificate>* certificate);

    // The ids of this vessel's soft-deleted certificates — the one question
    // list() exists to hide the answer to. CSV import needs the difference
    // between "this id was never here" and "it was here and was deleted"
    // (certificate-import-export-spec §5.1). Scope-filtered exactly as list()
    // is, and read once per import rather than once per row.
    bool listDeletedIds(const QString& vesselId, QSet<QString>* ids);

    // Writes refuse outright, with a message, when the certificate belongs to
    // another vessel under VESSEL mode. A silent empty result would be wrong
    // here: the user asked for something to be saved.
    bool create(const Certificate& certificate, QString* newId = nullptr);

    // Bumps revision and refreshes updated_at/updated_by, leaving created_at
    // and created_by alone (certificate-crud-spec §6).
    bool update(const Certificate& certificate);

    // Soft-deletes a certificate together with its endorsements, its active
    // short-term extension and its active attachment row, in one transaction
    // (certificate-attachment-addendum-2-spec §4 steps 1 to 3).
    //
    // All four tables belong to this module (CLAUDE.md §5), and a cascade is
    // the parent's business, so the SQL for all four lives here rather than
    // being three calls across three repositories with nothing holding them
    // together. One transaction is the point: a certificate that lost its
    // survey history but is still in the list would be a worse state than
    // either end of the operation.
    //
    // The *files* are not touched here. That is CertificateDeletionService's
    // job, and it runs afterwards: §4 is explicit that the database writes come
    // first, the opposite way round from attaching, so a failure can never find
    // the document already destroyed.
    //
    // Never a hard DELETE, for all the usual reasons (CLAUDE.md §6.4): the
    // deletion has to replicate to the other side and the audit trail has to
    // survive it.
    bool softDeleteWithChildren(const QString& id);

    QString errorString() const;

private:
    bool validate(const Certificate& certificate);
    bool checkVesselAllowed(const QString& vesselId);
    bool isScopedToOneVessel() const;

    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    QString             m_errorString;
};
