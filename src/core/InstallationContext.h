#pragma once

#include <QString>

// Which of the two installation modes this copy of the application is
// (CLAUDE.md §3). One binary, one of two identities, chosen once.
enum class InstallationMode {
    Office,
    Vessel
};

// The stored installation row, as plain data. Built by InstallationRepository
// — the only place allowed to touch SQL — and handed to InstallationContext.
struct InstallationRecord {
    QString          id;
    InstallationMode mode = InstallationMode::Office;
    QString          nodeId;
    QString          vesselId;          // empty when Office
    QString          vesselName;        // empty when Office
    QString          vesselImoNumber;   // empty when Office
};

// Read-only view of the installation identity, populated once at startup and
// never changed afterwards: installation mode is permanent
// (first-run-wizard-spec §7). There are no setters, so nothing later in the
// program can quietly reassign the vessel this copy belongs to.
class InstallationContext
{
public:
    // Default: not configured yet — the state before the wizard has run.
    InstallationContext() = default;
    explicit InstallationContext(const InstallationRecord& record);

    bool isConfigured() const;

    InstallationMode mode() const;
    QString          nodeId() const;
    QString          vesselId() const;
    QString          vesselName() const;
    QString          vesselImoNumber() const;

    // CLAUDE.md §3: the single accessor repositories consult to filter rows.
    // Returns the vessel id in VESSEL mode, and an empty string in OFFICE mode
    // meaning "no filter — the whole fleet is visible".
    //
    // Nothing consults this yet; the Vessel repository is the first, in step 4.
    QString vesselScope() const;

private:
    InstallationRecord m_record;
    bool               m_configured = false;
};
