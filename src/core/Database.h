#pragma once

#include <QSqlDatabase>
#include <QString>

// Owns the one SQLite connection the application uses.
//
// CLAUDE.md §2: SQLite via Qt SQL, office and vessel alike. The file lives in
// the per-user data folder chosen by QStandardPaths, so the same code finds a
// sensible location on Windows, macOS and Linux without a single #ifdef.
class Database
{
public:
    explicit Database(const QString& connectionName = QStringLiteral("ship_management"));
    ~Database();

    // Where the live database file belongs on this machine.
    static QString defaultFilePath();

    // Creates the containing folder if needed, opens the file, and turns on
    // foreign key enforcement. Returns false and sets errorString() on failure.
    bool open(const QString& filePath);
    void close();

    QSqlDatabase& connection();
    bool          isOpen() const;
    QString       filePath() const;
    QString       errorString() const;

private:
    // A database connection must not be copied: two Database objects closing
    // the same connection would be a double free in slow motion.
    Q_DISABLE_COPY(Database)

    QSqlDatabase m_connection;
    QString      m_connectionName;
    QString      m_filePath;
    QString      m_errorString;
};
