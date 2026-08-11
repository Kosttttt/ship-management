#include "core/MigrationRunner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>

#include <algorithm>

namespace {

// Splits a script into individual statements. QSqlQuery::exec() runs exactly
// one statement per call, so a migration containing a table and an index has
// to be broken up first.
//
// Quoted strings and comments are tracked so that a semicolon inside either is
// not mistaken for a statement boundary. Known limitation: a compound
// BEGIN ... END block (a trigger) would be split incorrectly. No migration
// needs one yet; when one does, it gets its own file and this grows a case.
QStringList splitStatements(const QString& script)
{
    QStringList statements;
    QString     current;
    bool        inLineComment  = false;
    bool        inBlockComment = false;
    bool        inQuotedText   = false;

    for (int i = 0; i < script.size(); ++i) {
        const QChar ch   = script.at(i);
        const QChar next = (i + 1 < script.size()) ? script.at(i + 1) : QChar();

        if (inLineComment) {
            if (ch == QLatin1Char('\n')) {
                inLineComment = false;
                current.append(ch);
            }
        } else if (inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
        } else if (inQuotedText) {
            current.append(ch);
            if (ch == QLatin1Char('\'')) {
                inQuotedText = false;
            }
        } else if (ch == QLatin1Char('-') && next == QLatin1Char('-')) {
            inLineComment = true;
            ++i;
        } else if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlockComment = true;
            ++i;
        } else if (ch == QLatin1Char(';')) {
            statements.append(current.trimmed());
            current.clear();
        } else {
            if (ch == QLatin1Char('\'')) {
                inQuotedText = true;
            }
            current.append(ch);
        }
    }
    statements.append(current.trimmed());

    statements.removeAll(QString());
    return statements;
}

// Line endings differ between a Windows checkout and a Linux one. Hashing the
// raw bytes would report an unmodified migration as tampered with the first
// time the repository is cloned on another platform.
QString checksumOf(const QString& sql)
{
    QString normalised = sql;
    normalised.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QByteArray digest =
        QCryptographicHash::hash(normalised.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

} // namespace

MigrationRunner::MigrationRunner(QSqlDatabase& database, const QString& migrationsDirectory)
    : m_database(database)
    , m_migrationsDirectory(migrationsDirectory)
{
}

bool MigrationRunner::run()
{
    m_errorString.clear();
    m_applied.clear();

    if (!m_database.isOpen()) {
        m_errorString = QStringLiteral("The database is not open, so migrations cannot run.");
        return false;
    }

    if (!ensureSchemaVersionTable()) {
        return false;
    }

    QList<MigrationFile> files;
    if (!discoverMigrations(&files)) {
        return false;
    }

    QHash<int, QString> recorded;
    if (!readRecordedChecksums(&recorded)) {
        return false;
    }

    for (const MigrationFile& file : files) {
        QString sql;
        if (!readMigrationFile(file, &sql)) {
            return false;
        }

        const QString checksum = checksumOf(sql);
        const auto    existing = recorded.constFind(file.version);

        if (existing != recorded.constEnd()) {
            if (*existing != checksum) {
                m_errorString =
                    QStringLiteral("Migration %1 has been changed since it was applied to this "
                                   "database.\n\nA migration that has already run must never be "
                                   "edited; add a new numbered migration instead.")
                        .arg(file.fileName);
                return false;
            }
            continue;
        }

        if (!applyMigration(file, sql, checksum)) {
            return false;
        }
        m_applied.append(file.fileName);
    }

    return true;
}

bool MigrationRunner::ensureSchemaVersionTable()
{
    // Created here rather than in a migration, because the runner has to read
    // this table to discover which migrations have run. Carries the standard
    // columns from CLAUDE.md §6.5 like every other table.
    static const QString kCreate = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "    id           TEXT PRIMARY KEY NOT NULL,"
        "    version      INTEGER NOT NULL UNIQUE,"
        "    file_name    TEXT NOT NULL,"
        "    checksum     TEXT NOT NULL,"
        "    created_at   TEXT NOT NULL,"
        "    created_by   TEXT NOT NULL,"
        "    updated_at   TEXT NOT NULL,"
        "    updated_by   TEXT NOT NULL,"
        "    is_deleted   INTEGER NOT NULL DEFAULT 0,"
        "    origin_node  TEXT NOT NULL,"
        "    revision     INTEGER NOT NULL DEFAULT 1"
        ")");

    QSqlQuery query(m_database);
    if (!query.exec(kCreate)) {
        m_errorString = QStringLiteral("Could not create the schema_version table:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}

bool MigrationRunner::discoverMigrations(QList<MigrationFile>* files)
{
    QDir dir(m_migrationsDirectory);
    if (!dir.exists()) {
        m_errorString = QStringLiteral("The migrations folder was not found:\n%1")
                            .arg(QDir::toNativeSeparators(m_migrationsDirectory));
        return false;
    }

    static const QRegularExpression namePattern(QStringLiteral("^(\\d+)_.+\\.sql$"));
    QHash<int, QString>             seenVersions;

    const QFileInfoList entries =
        dir.entryInfoList({QStringLiteral("*.sql")}, QDir::Files, QDir::Name);

    for (const QFileInfo& entry : entries) {
        const QRegularExpressionMatch match = namePattern.match(entry.fileName());
        if (!match.hasMatch()) {
            m_errorString = QStringLiteral("Migration file %1 is not named NNN_description.sql")
                                .arg(entry.fileName());
            return false;
        }

        MigrationFile file;
        file.version  = match.captured(1).toInt();
        file.fileName = entry.fileName();
        file.filePath = entry.absoluteFilePath();

        if (seenVersions.contains(file.version)) {
            m_errorString = QStringLiteral("Two migrations share the number %1: %2 and %3")
                                .arg(file.version)
                                .arg(seenVersions.value(file.version), file.fileName);
            return false;
        }
        seenVersions.insert(file.version, file.fileName);
        files->append(file);
    }

    std::sort(files->begin(), files->end(),
              [](const MigrationFile& a, const MigrationFile& b) { return a.version < b.version; });
    return true;
}

bool MigrationRunner::readRecordedChecksums(QHash<int, QString>* checksums)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT version, checksum FROM schema_version"))) {
        m_errorString = QStringLiteral("Could not read the schema_version table:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        checksums->insert(query.value(0).toInt(), query.value(1).toString());
    }
    return true;
}

bool MigrationRunner::readMigrationFile(const MigrationFile& file, QString* sql)
{
    QFile handle(file.filePath);
    if (!handle.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_errorString = QStringLiteral("Could not read migration %1:\n%2")
                            .arg(file.fileName, handle.errorString());
        return false;
    }

    QTextStream stream(&handle);
    stream.setEncoding(QStringConverter::Utf8);
    *sql = stream.readAll();
    return true;
}

bool MigrationRunner::applyMigration(const MigrationFile& file,
                                     const QString&       sql,
                                     const QString&       checksum)
{
    // One transaction per migration. If number 5 fails, migrations 1 to 4 stay
    // applied and recorded; only the broken one is undone. SQLite rolls back
    // CREATE TABLE as readily as INSERT, so a half-built schema is impossible.
    if (!m_database.transaction()) {
        m_errorString = QStringLiteral("Could not start a transaction for migration %1:\n%2")
                            .arg(file.fileName, m_database.lastError().text());
        return false;
    }

    const QStringList statements = splitStatements(sql);
    for (const QString& statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            const QString reason = query.lastError().text();
            m_database.rollback();
            m_errorString = QStringLiteral("Migration %1 failed and was rolled back.\n\n"
                                           "Statement:\n%2\n\nThe database reported:\n%3")
                                .arg(file.fileName, statement, reason);
            return false;
        }
    }

    if (!recordMigration(file, checksum)) {
        m_database.rollback();
        return false;
    }

    if (!m_database.commit()) {
        m_errorString = QStringLiteral("Could not commit migration %1:\n%2")
                            .arg(file.fileName, m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    return true;
}

bool MigrationRunner::recordMigration(const MigrationFile& file, const QString& checksum)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO schema_version"
        " (id, version, file_name, checksum, created_at, created_by,"
        "  updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, 'SYSTEM', ?, 'SYSTEM', 0, 'LOCAL', 1)"));
    insert.addBindValue(id);
    insert.addBindValue(file.version);
    insert.addBindValue(file.fileName);
    insert.addBindValue(checksum);
    insert.addBindValue(now);
    insert.addBindValue(now);

    if (!insert.exec()) {
        m_errorString = QStringLiteral("Could not record migration %1 in schema_version:\n%2")
                            .arg(file.fileName, insert.lastError().text());
        return false;
    }
    return true;
}

QString MigrationRunner::errorString() const
{
    return m_errorString;
}

QStringList MigrationRunner::appliedInLastRun() const
{
    return m_applied;
}
