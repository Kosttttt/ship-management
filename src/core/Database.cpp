#include "core/Database.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

Database::Database(const QString& connectionName)
    : m_connectionName(connectionName)
{
}

Database::~Database()
{
    close();
}

QString Database::defaultFilePath()
{
    // On Windows:  C:/Users/<user>/AppData/Roaming/<Org>/<App>
    // On macOS:    ~/Library/Application Support/<Org>/<App>
    // On Linux:    ~/.local/share/<Org>/<App>
    // The <Org>/<App> part comes from the names set in main().
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dataDir).filePath(QStringLiteral("ship-management.db"));
}

bool Database::open(const QString& filePath)
{
    m_errorString.clear();

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_errorString = QStringLiteral(
            "The SQLite database driver (QSQLITE) is not available in this Qt installation.");
        return false;
    }

    const QDir folder = QFileInfo(filePath).absoluteDir();
    if (!folder.exists() && !QDir().mkpath(folder.absolutePath())) {
        m_errorString = QStringLiteral("Could not create the data folder:\n%1")
                            .arg(QDir::toNativeSeparators(folder.absolutePath()));
        return false;
    }

    m_connection = QSqlDatabase::contains(m_connectionName)
                       ? QSqlDatabase::database(m_connectionName)
                       : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);

    m_connection.setDatabaseName(filePath);

    if (!m_connection.open()) {
        m_errorString = QStringLiteral("Could not open the database file:\n%1\n\n%2")
                            .arg(QDir::toNativeSeparators(filePath),
                                 m_connection.lastError().text());
        return false;
    }

    // SQLite ignores foreign keys unless asked, per connection, every time.
    QSqlQuery pragma(m_connection);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        m_errorString = QStringLiteral("Could not enable foreign key enforcement:\n%1")
                            .arg(pragma.lastError().text());
        return false;
    }

    m_filePath = filePath;
    return true;
}

void Database::close()
{
    const QString name = m_connectionName;

    if (m_connection.isValid()) {
        if (m_connection.isOpen()) {
            m_connection.close();
        }
        // Drop our handle before removing the connection, otherwise Qt warns
        // that the connection is still in use.
        m_connection = QSqlDatabase();
    }

    if (QSqlDatabase::contains(name)) {
        QSqlDatabase::removeDatabase(name);
    }
}

QSqlDatabase& Database::connection()
{
    return m_connection;
}

bool Database::isOpen() const
{
    return m_connection.isValid() && m_connection.isOpen();
}

QString Database::filePath() const
{
    return m_filePath;
}

QString Database::errorString() const
{
    return m_errorString;
}
