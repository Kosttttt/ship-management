#pragma once

#include "core/InstallationContext.h"

#include <QString>
#include <QWizardPage>

class QRadioButton;
class QSqlDatabase;

// Page 1 of the first-run wizard: OFFICE or VESSEL
// (first-run-wizard-spec §4).
//
// It also offers "Restore from a backup instead…" (database-backup-spec
// §10.2). On a brand-new computer the wizard is the only screen there is —
// Settings, where Restore otherwise lives, cannot be reached until setup is
// finished — so without this, moving to a new computer would mean inventing
// an installation just to restore over it.
class InstallationModePage : public QWizardPage
{
    Q_OBJECT

public:
    // connection and dataFolder are only for the restore button, and are
    // passed in rather than looked up so that only main() names the real
    // data folder (database-backup-spec §6).
    InstallationModePage(QSqlDatabase& connection, const QString& dataFolder,
                         QWidget* parent = nullptr);

    InstallationMode selectedMode() const;

    // OFFICE has nothing more to ask, so it skips straight to Finish.
    int nextId() const override;

private:
    // Runs the same sequence the Settings section runs, minus the "this
    // computer now" column, and asks the wizard to close when the restore is
    // staged (§10.2).
    void restoreFromBackup();

    QSqlDatabase& m_connection;
    QString       m_dataFolder;

    QRadioButton* m_officeButton = nullptr;
    QRadioButton* m_vesselButton = nullptr;
};
