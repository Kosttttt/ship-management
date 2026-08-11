#include "app/MainWindow.h"
#include "core/Database.h"
#include "core/Logger.h"
#include "core/MigrationRunner.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

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

// Opens the database and brings the schema up to date. Returns false after
// showing an explanatory dialog.
bool prepareDatabase(Database& database)
{
    const QString path = Database::defaultFilePath();

    if (!database.open(path)) {
        // Logged before the dialog is shown: a modal dialog blocks until the
        // user answers it, and a user who force-closes the application instead
        // would otherwise leave nothing behind to diagnose.
        qCritical() << "Database could not be opened:" << database.errorString();
        QMessageBox::critical(nullptr,
                              QObject::tr("Cannot start"),
                              QObject::tr("The database could not be opened.\n\n%1")
                                  .arg(database.errorString()));
        return false;
    }
    qInfo() << "Database opened:" << path;

    MigrationRunner runner(database.connection(), QString::fromLatin1(kMigrationsPath));
    if (!runner.run()) {
        qCritical() << "Migrations failed:" << runner.errorString();
        QMessageBox::critical(nullptr,
                              QObject::tr("Cannot start"),
                              QObject::tr("The database could not be brought up to date.\n\n%1")
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
        qCritical() << "Startup aborted.";
        Logger::shutdown();
        return 1;
    }

    MainWindow window(database.filePath());
    window.show();

    const int result = app.exec();

    qInfo() << "Ship Management System closing.";
    Logger::shutdown();
    return result;
}
