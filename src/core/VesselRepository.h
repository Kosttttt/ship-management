#pragma once

#include "core/Vessel.h"

#include <QList>
#include <QString>

#include <optional>

class InstallationContext;
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
    bool update(const Vessel& vessel);

    QString errorString() const;

private:
    bool validate(const Vessel& vessel);
    bool isImoNumberTaken(const QString& imoNumber, const QString& excludeId, bool* taken);
    bool isScopedToOneVessel() const;

    QSqlDatabase&              m_database;
    const InstallationContext& m_installation;
    QString                    m_errorString;
};
