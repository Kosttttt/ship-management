#include "DocumentTestSupport.h"

#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §4.1 and §7: version 1.0's two tables are
// dropped by a later migration, because 012 has already run on the
// developer's machine and a committed migration is never edited
// (CLAUDE.md §6.6).
//
// The case that matters is the one the developer's own database is in: 012's
// tables exist **with rows in them**, because the library was built and used
// before it was replaced.
class TestDocumentMigrations : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void theDropRunsOnADatabaseWithRowsInTheOldTables();
    void theDropRunsOnADatabaseThatNeverHadThem();
    void theNewTablesAreThereAfterwards();
    void migrationsAreIdempotentOnAnAlreadyCurrentDatabase();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString m_connectionName;
};

void TestDocumentMigrations::cleanup()
{
    if (!m_connectionName.isEmpty()) {
        closeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

void TestDocumentMigrations::theDropRunsOnADatabaseWithRowsInTheOldTables()
{
    // The developer's machine, as it actually is: migrated to 012, with a
    // document and a version in it from trying the library out.
    m_connectionName = QStringLiteral("docmig_withrows");
    QVERIFY(openDatabaseAtVersion(m_connectionName, 12));

    QSqlDatabase db = database();
    QVERIFY(tableExists(db, QStringLiteral("document")));
    QVERIFY(tableExists(db, QStringLiteral("document_version")));

    QSqlQuery insertDocument(db);
    insertDocument.prepare(QStringLiteral(
        "INSERT INTO document (id, vessel_id, category, title,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES ('d1', NULL, 'SMS', 'Old Policy',"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    QVERIFY2(insertDocument.exec(), qPrintable(insertDocument.lastError().text()));

    QSqlQuery insertVersion(db);
    insertVersion.prepare(QStringLiteral(
        "INSERT INTO document_version (id, document_id, sequence, file_path, original_file_name,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES ('v1', 'd1', 1, 'Fleet/SMS/a.pdf', 'a.pdf',"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    QVERIFY2(insertVersion.exec(), qPrintable(insertVersion.lastError().text()));

    // What a program start does after an update.
    QString error;
    QVERIFY2(runRemainingMigrations(db, &error), qPrintable(error));

    // Gone, rows and all. §4.1 says so out loud: the test documents are
    // lost, and their files stay in the old folder for the developer to
    // delete by hand.
    QVERIFY(!tableExists(db, QStringLiteral("document")));
    QVERIFY(!tableExists(db, QStringLiteral("document_version")));
}

void TestDocumentMigrations::theDropRunsOnADatabaseThatNeverHadThem()
{
    // A fresh installation, where 012 and 013 run one after the other and
    // there is nothing to drop. `DROP TABLE IF EXISTS` is what makes that a
    // no-op rather than an error that would stop the program starting.
    m_connectionName = QStringLiteral("docmig_fresh");
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    QVERIFY(!tableExists(db, QStringLiteral("document")));
    QVERIFY(!tableExists(db, QStringLiteral("document_version")));
}

void TestDocumentMigrations::theNewTablesAreThereAfterwards()
{
    m_connectionName = QStringLiteral("docmig_new");
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    for (const QString& table : {QStringLiteral("document_space"),
                                 QStringLiteral("document_page"),
                                 QStringLiteral("document_page_revision"),
                                 QStringLiteral("document_space_revision"),
                                 QStringLiteral("document_image")}) {
        QVERIFY2(tableExists(db, table), qPrintable(table));
    }
}

void TestDocumentMigrations::migrationsAreIdempotentOnAnAlreadyCurrentDatabase()
{
    m_connectionName = QStringLiteral("docmig_twice");
    QVERIFY(openDatabase(m_connectionName));

    // Running them again is what every start of the program does. It must
    // apply nothing and change nothing — and in particular 013 must not try
    // to drop a table it already dropped.
    QSqlDatabase db = database();
    QString      error;
    QVERIFY2(runRemainingMigrations(db, &error), qPrintable(error));

    QVERIFY(tableExists(db, QStringLiteral("document_space")));
    QVERIFY(!tableExists(db, QStringLiteral("document")));
}

// QTEST_MAIN, not QTEST_APPLESS_MAIN: no widgets here, but opening a SQLite
// connection needs an application object.
QTEST_MAIN(TestDocumentMigrations)
#include "tst_DocumentMigrations.moc"
