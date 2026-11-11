#include "core/BackupFileInspector.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace {

// Opens one file read-only on a connection name of its own and takes it away
// again at the end of the scope.
//
// A name of its own, generated per use, because the live database is already
// registered under "ship_management" and two inspections could otherwise run
// at once and tread on each other. Removing the connection matters as much as
// closing it: on Windows a file still held open by SQLite cannot be deleted,
// and DatabaseBackup has to be able to delete a backup that failed its check.
class ReadOnlyConnection
{
public:
    explicit ReadOnlyConnection(const QString& filePath)
        : m_name(QStringLiteral("backup_inspect_%1")
                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
    {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
            m_error = QStringLiteral(
                "The SQLite database driver (QSQLITE) is not available in this Qt installation.");
            return;
        }
        if (!QFileInfo::exists(filePath)) {
            m_error = QStringLiteral("There is no file at:\n%1")
                          .arg(QDir::toNativeSeparators(filePath));
            return;
        }

        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_database.setDatabaseName(filePath);
        // Read-only, so nothing here can alter a backup — and so a file on a
        // read-only medium (a CD, a locked share) can still be inspected.
        m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

        if (!m_database.open()) {
            m_error = QStringLiteral("The file could not be opened as a database:\n%1\n\n%2")
                          .arg(QDir::toNativeSeparators(filePath), m_database.lastError().text());
        }
    }

    ~ReadOnlyConnection()
    {
        if (m_database.isValid()) {
            if (m_database.isOpen()) {
                m_database.close();
            }
            m_database = QSqlDatabase();
        }
        if (QSqlDatabase::contains(m_name)) {
            QSqlDatabase::removeDatabase(m_name);
        }
    }

    ReadOnlyConnection(const ReadOnlyConnection&)            = delete;
    ReadOnlyConnection& operator=(const ReadOnlyConnection&) = delete;

    bool          isOpen() const { return m_database.isValid() && m_database.isOpen(); }
    QSqlDatabase& database() { return m_database; }
    QString       errorString() const { return m_error; }

private:
    QString      m_name;
    QSqlDatabase m_database;
    QString      m_error;
};

// One scalar, or false with the SQL error. Used for every count below, so a
// broken file reports the database's own words rather than a zero that looks
// like real data.
bool readScalar(QSqlDatabase& database, const QString& sql, QVariant* value, QString* errorString)
{
    QSqlQuery query(database);
    if (!query.exec(sql) || !query.next()) {
        *errorString = query.lastError().text();
        return false;
    }
    *value = query.value(0);
    return true;
}

bool hasOurTables(QSqlDatabase& database, QString* missingTable)
{
    // The four §5.1 names. Enough to tell this program's database from any
    // other SQLite file without demanding tables that a legitimately older
    // backup may not have yet.
    const QStringList required{QStringLiteral("schema_version"), QStringLiteral("installation"),
                               QStringLiteral("vessel"), QStringLiteral("certificate")};

    for (const QString& table : required) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?"));
        query.addBindValue(table);
        if (!query.exec() || !query.next() || query.value(0).toInt() != 1) {
            *missingTable = table;
            return false;
        }
    }
    return true;
}

} // namespace

QString BackupSummary::modeLabel() const
{
    return isVesselMode ? QStringLiteral("VESSEL") : QStringLiteral("OFFICE");
}

bool BackupFileInspector::passesIntegrityCheck(const QString& filePath, QString* errorString)
{
    ReadOnlyConnection connection(filePath);
    if (!connection.isOpen()) {
        *errorString = connection.errorString();
        return false;
    }

    QVariant result;
    QString  sqlError;
    if (!readScalar(connection.database(), QStringLiteral("PRAGMA integrity_check"), &result,
                    &sqlError)) {
        // Where a file that is not a database at all ends up: SQLite opens
        // lazily, so this is the first statement that actually reads it.
        *errorString = QStringLiteral("The file could not be read as a database:\n%1").arg(sqlError);
        return false;
    }

    // SQLite answers the single word "ok" when all is well, and otherwise one
    // row per problem found.
    const QString answer = result.toString();
    if (answer.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        *errorString = QStringLiteral("The database file is damaged:\n%1").arg(answer);
        return false;
    }
    return true;
}

bool BackupFileInspector::summarise(QSqlDatabase& connection, BackupSummary* summary,
                                    QString* errorString)
{
    *summary = BackupSummary();

    QSqlQuery installation(connection);
    installation.prepare(QStringLiteral(
        "SELECT i.installation_mode, i.node_id, v.name"
        "  FROM installation i"
        "  LEFT JOIN vessel v ON v.id = i.vessel_id"));
    if (!installation.exec()) {
        *errorString = QStringLiteral("The installation row could not be read:\n%1")
                           .arg(installation.lastError().text());
        return false;
    }
    if (!installation.next()) {
        *errorString = QStringLiteral(
            "This database has no installation row, so it is not a complete backup.");
        return false;
    }

    summary->isVesselMode =
        installation.value(0).toString().compare(QStringLiteral("VESSEL"), Qt::CaseInsensitive) == 0;
    summary->nodeId     = installation.value(1).toString();
    summary->vesselName = installation.value(2).toString();

    if (installation.next()) {
        // The table has a singleton constraint, so this needs a hand-edited
        // file to happen — which is exactly the sort of file worth refusing.
        *errorString = QStringLiteral(
            "This database has more than one installation row, so which computer it belongs to "
            "cannot be decided.");
        return false;
    }

    // Soft-deleted rows are excluded: these numbers are shown to a person
    // choosing between two databases, and a count including deleted rows
    // would not match what either screen shows (CLAUDE.md §6.4).
    QVariant value;
    QString  sqlError;

    if (!readScalar(connection, QStringLiteral("SELECT COUNT(*) FROM vessel WHERE is_deleted = 0"),
                    &value, &sqlError)) {
        *errorString = QStringLiteral("The vessels could not be counted:\n%1").arg(sqlError);
        return false;
    }
    summary->vesselCount = value.toInt();

    if (!readScalar(connection,
                    QStringLiteral("SELECT COUNT(*) FROM certificate WHERE is_deleted = 0"), &value,
                    &sqlError)) {
        *errorString = QStringLiteral("The certificates could not be counted:\n%1").arg(sqlError);
        return false;
    }
    summary->certificateCount = value.toInt();

    if (!readScalar(connection, QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_version"),
                    &value, &sqlError)) {
        *errorString = QStringLiteral("The schema version could not be read:\n%1").arg(sqlError);
        return false;
    }
    summary->schemaVersion = value.toInt();

    // ISO-8601 sorts the same way it reads, so MAX() over the text is the
    // latest instant without any date parsing in SQL.
    if (!readScalar(connection,
                    QStringLiteral("SELECT MAX(updated_at) FROM ("
                                   "  SELECT updated_at FROM vessel"
                                   "  UNION ALL"
                                   "  SELECT updated_at FROM certificate)"),
                    &value, &sqlError)) {
        *errorString = QStringLiteral("The last change could not be read:\n%1").arg(sqlError);
        return false;
    }
    const QString stored = value.toString();
    if (!stored.isEmpty()) {
        // Every timestamp is written with QDateTime::currentDateTimeUtc(),
        // whose ISO-8601 form ends in "Z" (CLAUDE.md §6.2), so this comes
        // back as UTC without being told.
        summary->lastChangeUtc = QDateTime::fromString(stored, Qt::ISODate);
    }

    return true;
}

bool BackupFileInspector::inspect(const QString& filePath, int highestKnownMigration,
                                  BackupSummary* summary, QString* errorString)
{
    *summary = BackupSummary();

    // Check 1. Its own connection, opened and taken away again inside the
    // call, so nothing is holding the file when the later checks run.
    if (!passesIntegrityCheck(filePath, errorString)) {
        return false;
    }

    ReadOnlyConnection connection(filePath);
    if (!connection.isOpen()) {
        *errorString = connection.errorString();
        return false;
    }

    // Check 2, first half.
    QString missingTable;
    if (!hasOurTables(connection.database(), &missingTable)) {
        *errorString = QStringLiteral(
                           "This file is a database, but not one belonging to this program: it has "
                           "no '%1' table.")
                           .arg(missingTable);
        return false;
    }

    // Check 2, second half, and the §5.2 numbers in the same pass.
    if (!summarise(connection.database(), summary, errorString)) {
        return false;
    }

    // Check 3. An older backup is fine; a newer one is not, because this
    // program has no idea what a migration it does not carry did to the data.
    if (summary->schemaVersion > highestKnownMigration) {
        *errorString = QStringLiteral(
                           "This backup was made by a newer version of the program (database "
                           "version %1, this program knows %2). Update this program first.")
                           .arg(summary->schemaVersion)
                           .arg(highestKnownMigration);
        return false;
    }

    return true;
}
