#pragma once

#include "core/InstallationContext.h"
#include "core/Vessel.h"

#include <QList>
#include <QString>

#include <optional>

class QSqlDatabase;

// Reads and writes the vessel table. The only file containing SQL for it
// (CLAUDE.md §4 rule 2).
//
// This is the first consumer of InstallationContext::vesselScope(). The
// VESSEL-mode filter is applied here, on every query, in one place — never in
// a screen (CLAUDE.md §3). A screen that forgot would leak another vessel's
// data to a crew member.
class VesselRepository
{
public:
    VesselRepository(QSqlDatabase& database, const InstallationContext& installation);

    // Both reads apply the scope filter and exclude soft-deleted rows.
    bool list(QList<Vessel>* vessels);
    bool findById(const QString& id, std::optional<Vessel>* vessel);

    // OFFICE only: a VESSEL installation manages one ship and never adds
    // another. Fills newId with the generated UUID on success.
    bool create(const Vessel& vessel, QString* newId = nullptr);

    // Bumps revision and refreshes updated_at/updated_by, leaving created_at
    // and created_by as they were (vessel-crud-spec §6).
    //
    // Deliberately does NOT write attachment_root_path, even though the column
    // is read into Vessel. VesselEditForm::vessel() builds a fresh Vessel out
    // of its own fields, so a path included here would be silently blanked
    // every time somebody corrected a call sign. The one place that path is
    // written is setAttachmentRootPath() below.
    bool update(const Vessel& vessel);

    // certificate-attachment-addendum-spec §2.3/§2.5: one column, on its own.
    //
    // Separate from update() because of the order §2.5 insists on — the files
    // are moved first, and this is called only once that has actually
    // succeeded. An empty path stores SQL NULL, which reads back as "not
    // configured".
    //
    // This does not move anything itself. VesselAttachmentRoots::changeRoot()
    // is what sequences the move and this write; calling this directly would
    // leave the database claiming a folder the files are not in.
    bool setAttachmentRootPath(const QString& vesselId, const QString& absolutePath);

    QString errorString() const;

private:
    bool validate(const Vessel& vessel);
    bool isImoNumberTaken(const QString& imoNumber, const QString& excludeId, bool* taken);
    bool isScopedToOneVessel() const;

    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    QString             m_errorString;
};
