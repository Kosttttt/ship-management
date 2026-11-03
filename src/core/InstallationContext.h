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
//
// A class that keeps one of these holds it BY VALUE, never as a
// `const InstallationContext&` member, even though taking it as a
// `const InstallationContext&` *parameter* is right and unchanged
// (installation-context-storage-fix.md).
//
// A reference member only works while the caller keeps the original alive, and
// the natural way to construct a repository is
// `CertificateRepository repo(db, officeContext());` — where that argument is
// a temporary destroyed at the closing semicolon, leaving the member dangling.
// Every later call then reads freed memory. That survives an unoptimised build,
// because the stack slot usually still holds the old bytes, and breaks under
// -O2 where the slot gets reused: it turned into an access violation in one
// test suite and, worse, into VESSEL-mode queries that silently returned
// another vessel's rows — the exact failure CLAUDE.md §3's scope filtering
// exists to prevent.
//
// Holding it by value makes that impossible to write. It costs nothing: the
// fields are ref-counted QStrings, the copy happens once per construction, and
// because there are no setters a copy can never drift from its original.
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
