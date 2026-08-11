#include "core/MigrationRunner.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

// Each test gets a private in-memory database and a private folder of
// migration files, so no test can influence another and none of them touch the
// real application database.
class TestMigrationRunner : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void appliesMigrationToEmptyDatabase();
    void recordsVersionAndFileName();
    void secondRunAppliesNothing();
    void appliesInNumericOrder();
    void appliesSeveralStatementsFromOneFile();
    void ignoresSemicolonsInCommentsAndStrings();
    void failedMigrationIsRolledBackEntirely();
    void rejectsEditedMigration();
    void rejectsBadlyNamedFile();
    void rejectsDuplicateVersionNumbers();
    void reportsMissingMigrationsFolder();

private:
    void writeMigration(const QString& fileName, const QString& sql);
    bool tableExists(const QString& tableName);
    int  rowCountOf(const QString& tableName);

    std::unique_ptr<QTemporaryDir> m_folder;
    QString                        m_connectionName;
};

void TestMigrationRunner::init()
{
    m_folder = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_folder->isValid(), "could not create a temporary migrations folder");

    m_connectionName = QStringLiteral("test_%1").arg(QTest::currentTestFunction());

    QSqlDatabase database =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(database.open(), "could not open an in-memory SQLite database");
}

void TestMigrationRunner::cleanup()
{
    {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName);
        if (database.isOpen()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_folder.reset();
}

void TestMigrationRunner::writeMigration(const QString& fileName, const QString& sql)
{
    QFile file(QDir(m_folder->path()).filePath(fileName));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text),
             qPrintable(QStringLiteral("could not write %1").arg(fileName)));
    file.write(sql.toUtf8());
    file.close();
}

bool TestMigrationRunner::tableExists(const QString& tableName)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM sqlite_master"
                                 " WHERE type = 'table' AND name = ?"));
    query.addBindValue(tableName);
    return query.exec() && query.next() && query.value(0).toInt() == 1;
}

int TestMigrationRunner::rowCountOf(const QString& tableName)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void TestMigrationRunner::appliesMigrationToEmptyDatabase()
{
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY);"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
    QVERIFY(tableExists(QStringLiteral("vessel")));
    QVERIFY(tableExists(QStringLiteral("schema_version")));
    QCOMPARE(runner.appliedInLastRun().size(), 1);
}

void TestMigrationRunner::recordsVersionAndFileName()
{
    writeMigration(QStringLiteral("007_create_thing.sql"),
                   QStringLiteral("CREATE TABLE thing (id TEXT PRIMARY KEY);"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));

    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT version, file_name, checksum, created_by, is_deleted FROM schema_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 7);
    QCOMPARE(query.value(1).toString(), QStringLiteral("007_create_thing.sql"));
    QVERIFY(!query.value(2).toString().isEmpty());
    QCOMPARE(query.value(3).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(4).toInt(), 0);
}

void TestMigrationRunner::secondRunAppliesNothing()
{
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY);"));

    QSqlDatabase database = QSqlDatabase::database(m_connectionName);

    MigrationRunner first(database, m_folder->path());
    QVERIFY2(first.run(), qPrintable(first.errorString()));
    QCOMPARE(first.appliedInLastRun().size(), 1);

    // The second run must be a no-op. If it re-ran the file, CREATE TABLE
    // would fail because the table already exists.
    MigrationRunner second(database, m_folder->path());
    QVERIFY2(second.run(), qPrintable(second.errorString()));
    QVERIFY(second.appliedInLastRun().isEmpty());
    QCOMPARE(rowCountOf(QStringLiteral("schema_version")), 1);
}

void TestMigrationRunner::appliesInNumericOrder()
{
    // 002 depends on the table 001 creates, so a wrong order fails outright.
    writeMigration(QStringLiteral("002_seed_vessel.sql"),
                   QStringLiteral("INSERT INTO vessel (id) VALUES ('abc');"));
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY);"));
    writeMigration(QStringLiteral("010_add_column.sql"),
                   QStringLiteral("ALTER TABLE vessel ADD COLUMN name TEXT;"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY2(runner.run(), qPrintable(runner.errorString()));

    const QStringList expected{QStringLiteral("001_create_vessel.sql"),
                               QStringLiteral("002_seed_vessel.sql"),
                               QStringLiteral("010_add_column.sql")};
    QCOMPARE(runner.appliedInLastRun(), expected);
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 1);
}

void TestMigrationRunner::appliesSeveralStatementsFromOneFile()
{
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY, imo TEXT);\n"
                                  "CREATE INDEX ix_vessel_imo ON vessel (imo);\n"
                                  "INSERT INTO vessel (id, imo) VALUES ('a', '9123456');\n"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
    QCOMPARE(rowCountOf(QStringLiteral("vessel")), 1);
}

void TestMigrationRunner::ignoresSemicolonsInCommentsAndStrings()
{
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("-- a comment; with a semicolon in it\n"
                                  "/* a block comment; also with one */\n"
                                  "CREATE TABLE vessel (id TEXT PRIMARY KEY, note TEXT);\n"
                                  "INSERT INTO vessel (id, note) VALUES ('a', 'hello; world');\n"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY2(runner.run(), qPrintable(runner.errorString()));

    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("SELECT note FROM vessel")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("hello; world"));
}

void TestMigrationRunner::failedMigrationIsRolledBackEntirely()
{
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY);"));
    // First statement is valid, second is not. Neither must survive.
    writeMigration(QStringLiteral("002_broken.sql"),
                   QStringLiteral("CREATE TABLE good_table (id TEXT PRIMARY KEY);\n"
                                  "CREATE TABLE oops (this is not valid sql);\n"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY(!runner.run());
    QVERIFY(runner.errorString().contains(QStringLiteral("002_broken.sql")));

    // 001 stayed applied; every part of 002 was undone.
    QVERIFY(tableExists(QStringLiteral("vessel")));
    QVERIFY(!tableExists(QStringLiteral("good_table")));
    QCOMPARE(rowCountOf(QStringLiteral("schema_version")), 1);
}

void TestMigrationRunner::rejectsEditedMigration()
{
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY);"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner first(database, m_folder->path());
    QVERIFY2(first.run(), qPrintable(first.errorString()));

    // CLAUDE.md §6.6: an applied migration must never be edited.
    writeMigration(QStringLiteral("001_create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY, extra TEXT);"));

    MigrationRunner second(database, m_folder->path());
    QVERIFY(!second.run());
    QVERIFY(second.errorString().contains(QStringLiteral("has been changed")));
}

void TestMigrationRunner::rejectsBadlyNamedFile()
{
    writeMigration(QStringLiteral("create_vessel.sql"),
                   QStringLiteral("CREATE TABLE vessel (id TEXT PRIMARY KEY);"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY(!runner.run());
    QVERIFY(runner.errorString().contains(QStringLiteral("NNN_description.sql")));
}

void TestMigrationRunner::rejectsDuplicateVersionNumbers()
{
    // Two developers each adding "003" is the classic merge accident.
    writeMigration(QStringLiteral("003_one.sql"),
                   QStringLiteral("CREATE TABLE one (id TEXT PRIMARY KEY);"));
    writeMigration(QStringLiteral("003_two.sql"),
                   QStringLiteral("CREATE TABLE two (id TEXT PRIMARY KEY);"));

    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, m_folder->path());

    QVERIFY(!runner.run());
    QVERIFY(runner.errorString().contains(QStringLiteral("share the number 3")));
    QVERIFY(!tableExists(QStringLiteral("one")));
}

void TestMigrationRunner::reportsMissingMigrationsFolder()
{
    QSqlDatabase    database = QSqlDatabase::database(m_connectionName);
    MigrationRunner runner(database, QStringLiteral("/no/such/folder/anywhere"));

    QVERIFY(!runner.run());
    QVERIFY(runner.errorString().contains(QStringLiteral("was not found")));
}

QTEST_MAIN(TestMigrationRunner)
#include "tst_MigrationRunner.moc"
