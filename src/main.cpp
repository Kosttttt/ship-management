#include "app/FirstRunWizard.h"
#include "app/MainWindow.h"
#include "app/ModuleRegistry.h"
#include "core/Database.h"
#include "core/InstallationContext.h"
#include "core/InstallationRepository.h"
#include "core/Logger.h"
#include "core/MigrationRunner.h"
#include "core/VesselRepository.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

#include <optional>

namespace {

// Migrations are compiled into the executable, so ":/migrations" is a folder
// inside the binary rather than on disk. An installed copy therefore cannot
// lose or half-copy its migrations, and there is no path to guess at runtime.
const char* const kMigrationsPath = ":/migrations";

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

    MigrationRunner runner(database.connection(), QString::fromLatin1(kMigrationsPath));
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
    FirstRunWizard wizard(repository);
    if (wizard.exec() != QDialog::Accepted) {
        return StartupOutcome::Cancelled;
    }

    *context = InstallationContext(wizard.record());
    return StartupOutcome::Ready;
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
    VesselRepository vessels(database.connection(), installation);
    ModuleRegistry   modules(database.connection(), installation);

    MainWindow window(installation, vessels, modules);
    window.show();

    const int result = app.exec();

    qInfo() << "Ship Management System closing.";
    Logger::shutdown();
    return result;
}
