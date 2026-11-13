#pragma once

#include "core/InstallationContext.h"

#include <QString>

class QSqlDatabase;
class VesselRepository;

// The setup step that gives every active vessel a space of its own
// (document-management-spec.md §4.3).
//
// Split out of DocumentSpaceRepository, which was 354 lines, by the rule in
// CLAUDE.md §9. The seam is a real one: everything left in that class answers
// a question about spaces that already exist, while this runs once when the
// screen opens and makes the ones that should be there. SQL, so it stays in
// the repository layer (CLAUDE.md §4 rule 2), and it is used only by
// `document_space`'s own repository's callers.
class DocumentVesselSpaces
{
public:
    DocumentVesselSpaces(QSqlDatabase& database, const InstallationContext& installation,
                         VesselRepository& vessels);

    // One Collection space per **active** vessel.
    //
    // Idempotent twice over: it only creates a space for a vessel that has
    // no row at all — deleted ones included, so a space cannot be
    // resurrected by reopening the screen — and `document_space.vessel_id`
    // is UNIQUE, so even a race could not produce two.
    //
    // Reports how many were created, which is what the test asserts on.
    bool ensureAll(int* createdCount = nullptr);

    QString errorString() const;

private:
    QSqlDatabase& m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    VesselRepository&   m_vessels;
    QString             m_errorString;
};
