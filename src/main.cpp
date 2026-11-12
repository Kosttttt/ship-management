#include "app/FirstRunWizard.h"
#include "app/MainWindow.h"
#include "app/ModuleRegistry.h"
#include "core/AutomaticBackup.h"
#include "core/BackupSettings.h"
#include "core/Database.h"
#include "core/InstallationContext.h"
#include "core/InstallationRepository.h"
#include "core/AppSettingRepository.h"
#include "core/Logger.h"
#include "core/MigrationRunner.h"
#include "core/RestoreSwap.h"
#include "core/VesselRepository.h"

#include <QApplication>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

#include <optional>

namespace {

// Where the live database file sits. The same call the database is opened
// with, so a restore swaps files in the right folder by construction rather
// than by a guess — which is the rule the whole backup feature follows
// (database-backup-spec §1, CLAUDE.md §10.1).
QString dataFolder()
{
    return QFileInfo(Database::defaultFilePath()).absolutePath();
}

void startLogging()
{
    const QString logDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("logs"));

    QString reason;
    if (!Logger::initialise(logDir, &reason)) {
        // Not fatal: an application that works but cannot write a log file is
        // far more useful than one that refuses to start.
        QMessageBox::warning(nullptr,
                             QObject::tr("Logging unavailable"),
                             QObject::tr("The application will run, but no log file will be "
                                         "written.\n\n%1")
                                 .arg(reason));
    }
}

void showStartupError(const QString& detail)
{
    // Logged before the dialog is shown: a modal dialog blocks until the user
    // answers it, and a user who force-closes the application instead would
    // otherwise leave nothing behind to diagnose.
    qCritical() << "Startup failed:" << detail;
    QMessageBox::critical(nullptr, QObject::tr("Cannot start"), detail);
}

// Opens the database and brings the schema up to date.
bool prepareDatabase(Database& database)
{
    const QString path = Database::defaultFilePath();

    if (!database.open(path)) {
        showStartupError(QObject::tr("The database could not be opened.\n\n%1")
                             .arg(database.errorString()));
        return false;
    }
    qInfo() << "Database opened:" << path;

    MigrationRunner runner(database.connection(), MigrationRunner::compiledInPath());
    if (!runner.run()) {
        showStartupError(QObject::tr("The database could not be brought up to date.\n\n%1")
                             .arg(runner.errorString()));
        return false;
    }

    const QStringList applied = runner.appliedInLastRun();
    if (applied.isEmpty()) {
        qInfo() << "Database schema is already up to date.";
    } else {
        qInfo() << "Applied migrations:" << applied.join(QStringLiteral(", "));
    }
    return true;
}

// first-run-wizard-spec §6. Zero installation rows means first run, and the
// wizard is the only thing allowed to write until one exists.
enum class StartupOutcome { Ready, Cancelled, Failed };

StartupOutcome resolveInstallation(Database& database, InstallationContext* context)
{
    InstallationRepository repository(database.connection());

    std::optional<InstallationRecord> existing;
    if (!repository.load(&existing)) {
        showStartupError(QObject::tr("The installation settings could not be read.\n\n%1")
                             .arg(repository.errorString()));
        return StartupOutcome::Failed;
    }

    if (existing.has_value()) {
        *context = InstallationContext(*existing);
        qInfo() << "Existing installation loaded:" << context->nodeId();
        return StartupOutcome::Ready;
    }

    qInfo() << "No installation row found — running the first-run wizard.";
    FirstRunWizard wizard(repository, database.connection(), dataFolder());
    if (wizard.exec() != QDialog::Accepted) {
        return StartupOutcome::Cancelled;
    }

    *context = InstallationContext(wizard.record());
    return StartupOutcome::Ready;
}

// database-backup-spec §5.3 step 3: before the database is opened, so a
// pending restore is swapped in while nothing is holding either file.
//
// Never fatal. A failure here means the restore did not happen and the
// original database is still in place, which is exactly what should then be
// opened.
void applyPendingRestore()
{
    QString detail;
    switch (RestoreSwap::applyPending(dataFolder(), &detail)) {
    case RestoreSwap::Outcome::NothingPending:
        return;
    case RestoreSwap::Outcome::Restored:
        qInfo() << "Restore completed at startup:" << detail;
        QMessageBox::information(nullptr, QObject::tr("Backup restored"), detail);
        return;
    case RestoreSwap::Outcome::Failed:
        qCritical() << "Restore failed at startup:" << detail;
        QMessageBox::warning(nullptr, QObject::tr("The backup was not restored"), detail);
        return;
    }
}

// database-backup-spec §4: after migrations, so the backup carries the
// current schema, and after the installation is resolved, so it is filed
// under this node's name.
//
// Returns a short line for the status bar, or an empty string when there is
// nothing worth saying. It never stops startup: every outcome, success or
// failure, ends in a sentence and a log entry.
QString runAutomaticBackup(Database& database, const InstallationContext& installation)
{
    if (!BackupSettings::automaticEnabled()) {
        qInfo() << "Automatic daily backup is switched off.";
        return QString();
    }

    QString saved;
    QString error;
    const AutomaticBackup::Outcome outcome =
        AutomaticBackup::runIfDue(database.connection(), BackupSettings::automaticFolder(),
                                  installation.nodeId(), QDate::currentDate(), &saved, &error);

    switch (outcome) {
    case AutomaticBackup::Outcome::AlreadyDoneToday:
        qInfo() << "Automatic backup: today's is already in the folder.";
        return QString();
    case AutomaticBackup::Outcome::Saved:
        qInfo() << "Automatic backup written:" << saved;
        return QObject::tr("Backed up to %1").arg(QDir::toNativeSeparators(saved));
    case AutomaticBackup::Outcome::NoFolder:
    case AutomaticBackup::Outcome::Failed:
        // The unplugged drive, the cloud folder that is not running. Worth
        // saying out loud, never worth refusing to start over.
        qWarning() << "Automatic backup did not run:" << error;
        return QObject::tr("Automatic backup did not run — see Settings.");
    }
    return QString();
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Qt uses these two names to decide where per-user files belong, which is
    // how the database and log folders get located below.
    QCoreApplication::setOrganizationName("Ship Management");
    QCoreApplication::setApplicationName("Ship Management System");

    startLogging();
    qInfo() << "Ship Management System starting.";

    // Before anything opens the database: a restore asked for in the last
    // session is finished here, while no connection is holding either file
    // (database-backup-spec §5.3 step 3).
    applyPendingRestore();

    Database database;
    if (!prepareDatabase(database)) {
        Logger::shutdown();
        return 1;
    }

    InstallationContext installation;
    switch (resolveInstallation(database, &installation)) {
    case StartupOutcome::Failed:
        Logger::shutdown();
        return 1;
    case StartupOutcome::Cancelled:
        // Not an error: the user declined to set the application up, and
        // without a recorded mode nothing else may run.
        qInfo() << "Setup cancelled — exiting without configuring.";
        Logger::shutdown();
        return 0;
    case StartupOutcome::Ready:
        break;
    }

    // Declared before the window so they outlive every screen holding a
    // reference to them. The registry owns each module, and each module owns
    // its own repositories.
    VesselRepository     vessels(database.connection(), installation);
    AppSettingRepository appSettings(database.connection());
    ModuleRegistry       modules(database.connection(), installation);

    MainWindow window(installation, vessels, appSettings, modules, database.connection(),
                      dataFolder());
    window.show();

    // After migrations and after the installation is resolved, so the copy
    // carries the current schema and is filed under this node's name
    // (database-backup-spec §4). Never allowed to stop the program starting,
    // which is why it runs here, with the window already up, and reports
    // through the status bar rather than a dialog.
    window.showStartupNotice(runAutomaticBackup(database, installation));

    const int result = app.exec();

    qInfo() << "Ship Management System closing.";
    Logger::shutdown();
    return result;
}
