#include "app/RestoreFromBackup.h"

#include "app/RestoreConfirmationDialog.h"
#include "core/BackupFileInspector.h"
#include "core/BackupSettings.h"
#include "core/MigrationRunner.h"
#include "core/RestoreSwap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSqlDatabase>

#include <optional>

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("RestoreFromBackup", text);
}

// §5.3 step 2. Nothing has been swapped at this point — the pending file is
// simply sitting beside the database — so a restart that cannot be launched
// is an inconvenience rather than a broken installation.
void offerTheRestart(QWidget* parent)
{
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList())) {
        qInfo() << "Restarting to finish a restore.";
        return;
    }

    qWarning() << "Could not restart to finish a restore; the pending file is still in place.";
    QMessageBox::information(parent, tr("Restart to finish"),
                             tr("The backup is ready to be restored, but the program could not "
                                "start itself again.\n\nStart the program again to finish "
                                "restoring."));
}

} // namespace

RestoreFromBackup::Outcome RestoreFromBackup::run(QWidget* parent, QSqlDatabase& connection,
                                                  const QString& dataFolder,
                                                  bool           showThisComputer)
{
    const QString chosen = QFileDialog::getOpenFileName(
        parent, tr("Choose a backup to restore"), BackupSettings::lastManualFolder(),
        tr("Database files (*.db);;All files (*)"));
    if (chosen.isEmpty()) {
        return Outcome::NothingDone; // cancelled
    }

    // §5.1 check 3 needs to know what this program carries. Reading the
    // migrations folder touches neither the database nor the backup.
    MigrationRunner runner(connection, MigrationRunner::compiledInPath());
    int             highestKnown = 0;
    if (!runner.highestAvailableVersion(&highestKnown)) {
        QMessageBox::warning(parent, tr("Could not check the backup"), runner.errorString());
        return Outcome::NothingDone;
    }

    // Every check, on a read-only connection of its own, before anything at
    // all is written.
    BackupSummary backup;
    QString       error;
    if (!BackupFileInspector::inspect(chosen, highestKnown, &backup, &error)) {
        QMessageBox::warning(parent, tr("This backup cannot be restored"), error);
        return Outcome::NothingDone;
    }

    std::optional<BackupSummary> current;
    if (showThisComputer) {
        BackupSummary summary;
        if (!BackupFileInspector::summarise(connection, &summary, &error)) {
            QMessageBox::warning(parent, tr("Could not read this computer's database"), error);
            return Outcome::NothingDone;
        }
        current = summary;
    }

    RestoreConfirmationDialog confirmation(current, backup, chosen, parent);
    if (confirmation.exec() != QDialog::Accepted) {
        return Outcome::NothingDone;
    }

    // §5.3 step 1. Still nothing replaced: the backup is copied in beside the
    // live database under a pending name, and the swap happens at the next
    // start, before anything opens it.
    if (!RestoreSwap::stage(dataFolder, chosen, &error)) {
        QMessageBox::warning(parent, tr("Could not prepare the restore"), error);
        return Outcome::NothingDone;
    }

    offerTheRestart(parent);
    return Outcome::Staged;
}
