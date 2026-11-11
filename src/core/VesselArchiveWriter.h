#pragma once

#include "core/InstallationContext.h"

#include <QString>

class QSqlDatabase;

// The guards and the write behind VesselRepository::archive() / restore()
// (vessel-archive-spec §3, §6; moved here for size by
// housekeeping-round-spec §2.2).
//
// In the repository layer and holding SQL for the vessel table, which
// CLAUDE.md §9 allows for a helper used only by its own repository.
// VesselRepository is the only thing that may use this one, and the public
// archive() / restore() stay on the repository.
class VesselArchiveWriter
{
public:
    VesselArchiveWriter(QSqlDatabase& database, const InstallationContext& installation);

    // Everything archive() and restore() have in common: the empty-id guard,
    // the OFFICE-only refusal, the pre-read, and the "is it already in that
    // state" check. Every message is derived from wantArchived rather than
    // passed in, so the two callers cannot drift apart in their wording.
    //
    // On success *imoNumber holds the vessel's number, which restore() needs
    // for its own IMO backstop. That is the only thing either caller wants
    // from the row, so the read fetches two columns rather than a whole
    // Vessel — and the one positional column list in VesselRepository.cpp
    // stays in the one file whose queries use it.
    bool checkChangeAllowed(const QString& vesselId, bool wantArchived, QString* imoNumber);

    bool writeFlag(const QString& vesselId, bool archived);

    QString errorString() const;

private:
    bool isScopedToOneVessel() const;

    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    QString             m_errorString;
};
