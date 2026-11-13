#pragma once

#include "core/InstallationContext.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QList>
#include <QString>

#include <optional>

class QSqlDatabase;
class VesselRepository;

// Reads and writes `document_space`. The only file holding SQL for it
// (CLAUDE.md §4 rule 2).
//
// **Who sees and may change what is enforced here** (§4.3, CLAUDE.md §3):
//
//   * OFFICE: everything. Creates, renames and deletes company spaces.
//   * VESSEL: company spaces **read-only** — they will arrive from the office
//     by package in step 6 — plus its own vessel space, which it may edit.
//     Another vessel's space is not shown at all.
//
// The "which vessel spaces are visible" half comes from VesselRepository,
// which already applies the installation's vessel filter on every query. So
// a VESSEL installation sees exactly one vessel and therefore exactly one
// vessel space, and an archived vessel disappears from both at once — one
// rule, in one place, rather than a second copy of it here.
class DocumentSpaceRepository
{
public:
    DocumentSpaceRepository(QSqlDatabase&              database,
                            const InstallationContext& installation,
                            VesselRepository&          vessels);

    // Company spaces first, then vessel spaces, each group by position and
    // then name. Only spaces this installation may see.
    bool list(QList<DocumentSpace>* spaces);

    bool findById(const QString& id, std::optional<DocumentSpace>* space);

    // OFFICE only: a vessel installation does not create company spaces, and
    // its own space is made for it.
    bool createCompanySpace(const QString& name, SpaceKind kind, const QString& description,
                            QString* newId = nullptr);

    bool rename(const QString& spaceId, const QString& name);

    // Soft delete (CLAUDE.md §6.4). The caller is responsible for the pages:
    // DocumentPageRepository::softDeleteSpacePages() takes them, and the
    // screen asks first (§5).
    //
    // A vessel space cannot be deleted — it belongs to the vessel, not to
    // whoever is looking at it, and an archived vessel's space is hidden
    // rather than removed (§4.3).
    bool softDelete(const QString& spaceId);

    // Giving each active vessel a space of its own is DocumentVesselSpaces,
    // beside this class (§4.3).

    // §4.3's write rule, for the screen to grey its buttons with. The
    // repository refuses regardless; this is so a person is not told off
    // after typing.
    bool mayEdit(const DocumentSpace& space) const;

    QString errorString() const;

private:
    bool isScopedToOneVessel() const;

    // The vessel ids this installation may see, from VesselRepository — which
    // excludes archived vessels and, in VESSEL mode, every vessel but its own.
    bool visibleVesselIds(QStringList* ids);

    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    VesselRepository&   m_vessels;
    QString             m_errorString;
};
