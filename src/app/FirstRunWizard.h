#pragma once

#include "core/InstallationContext.h"

#include <QString>
#include <QWizard>

class InstallationModePage;
class VesselIdentityPage;
class InstallationRepository;
class QSqlDatabase;

// Explicit page numbers rather than the ids QWizard would hand out, because
// InstallationModePage::nextId() has to name the page it jumps to.
enum class WizardPageId {
    Mode           = 0,
    VesselIdentity = 1
};

// Runs once, before MainWindow exists, when the installation table is empty
// (first-run-wizard-spec §4 and §6).
class FirstRunWizard : public QWizard
{
    Q_OBJECT

public:
    // connection and dataFolder reach the mode page's "Restore from a backup
    // instead…" button and nothing else (database-backup-spec §10.2). Passed
    // in rather than looked up, so only main() names the real data folder.
    FirstRunWizard(InstallationRepository& repository, QSqlDatabase& connection,
                   const QString& dataFolder, QWidget* parent = nullptr);

    // Valid only after exec() returned Accepted.
    InstallationRecord record() const;

protected:
    // The database write happens here, so a failure can keep the wizard open
    // instead of closing it on a promise it did not keep.
    void accept() override;

private:
    InstallationRepository& m_repository;
    InstallationModePage*   m_modePage     = nullptr;
    VesselIdentityPage*     m_identityPage = nullptr;
    InstallationRecord      m_record;
};
