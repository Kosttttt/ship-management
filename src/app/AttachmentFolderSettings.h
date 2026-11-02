#pragma once

#include "core/VesselAttachmentRoots.h"

#include <QWidget>

class QFormLayout;
class QLabel;
class VesselRepository;

// The per-vessel attachment folder control on the Settings screen
// (certificate-attachment-addendum-spec §2.1 and §2.2).
//
// One row per vessel: the vessel's name, the folder its documents are kept in,
// and a button to choose a different one. A VESSEL installation manages exactly
// one ship, so its scope filter leaves exactly one row and there is no list to
// speak of; an OFFICE installation gets the whole fleet, each vessel pointing
// wherever its own paperwork already lives (§2.1). Both come out of the same
// code path, because the difference is entirely in what VesselRepository::list()
// returns (CLAUDE.md §3).
//
// Its own widget rather than another two hundred lines inside SettingsPage,
// which would have taken that file past the ~300-line guideline in
// CLAUDE.md §9. The seam is a real one: everything here is about one vessel's
// files, and nothing here touches an alert threshold.
//
// It changes nothing itself. Choosing a folder goes to
// VesselAttachmentRoots::changeRoot(), which validates it, moves the vessel's
// existing documents and only then records the new path (§2.5).
class AttachmentFolderSettings : public QWidget
{
    Q_OBJECT

public:
    // No InstallationContext parameter, deliberately. VESSEL versus OFFICE is
    // already settled by VesselRepository::list(), whose scope filter returns
    // one vessel or the whole fleet (CLAUDE.md §3). Consulting the mode here
    // as well would be a second copy of a rule that is only allowed to live in
    // one place.
    explicit AttachmentFolderSettings(VesselRepository& vessels, QWidget* parent = nullptr);

    // Re-reads the fleet and every vessel's stored folder.
    void reload();

    // How many vessels are listed. Exposed so a test can assert the §2.1
    // difference between the two installation modes without reaching in for
    // widgets by name.
    int vesselRowCount() const;

private:
    // Prompts for a folder and hands it to changeRoot(), reporting whatever
    // comes back. Named per vessel because the dialog's title says which ship
    // it is about — an office with a fleet needs that.
    void chooseFolderFor(const QString& vesselId, const QString& vesselName);

    VesselRepository&     m_vessels;
    VesselAttachmentRoots m_roots;

    QFormLayout* m_rows        = nullptr;
    QLabel*      m_emptyLabel  = nullptr;
    int          m_vesselCount = 0;
};
