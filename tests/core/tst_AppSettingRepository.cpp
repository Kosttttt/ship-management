#include "core/AppSettingRepository.h"
#include "core/MigrationRunner.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

// settings-app-setting-spec §8, items 1 to 6.
class TestAppSettingRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void freshMigrationSeedsTheDefaults();                       // 1
    void refusesCriticalNotBelowExpiringSoon();                  // 2
    void refusesExpiringSoonNotBelowDueSoon();                   // 3
    void refusesZeroOrNegativeValues_data();                     // 4
    void refusesZeroOrNegativeValues();                          // 4
    void validUpdateIsStoredAndBumpsRevision();                  // 5
    void rawCheckConstraintTextNeverReachesTheUser();            // 6

    // Supporting the above.
    void theRowIsASingleton();
    void updateLeavesTheToastDateAlone();
    void readReportsAMissingRowRatherThanCrashing();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    int          revisionOf();

    QString m_connectionName;
};

void TestAppSettingRepository::init()
{
    m_connectionName = QStringLiteral("appsetting_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
}

void TestAppSettingRepository::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

int TestAppSettingRepository::revisionOf()
{
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT revision FROM app_setting")) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void TestAppSettingRepository::freshMigrationSeedsTheDefaults()
{
    // The migration seeds the row itself, so it exists before anything has
    // run — no wizard, no first save.
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);

    AppSetting setting;
    QVERIFY2(repository.read(&setting), qPrintable(repository.errorString()));

    QCOMPARE(setting.criticalDays, 30);
    QCOMPARE(setting.expiringSoonDays, 60);
    QCOMPARE(setting.dueSoonDays, 90);
    QVERIFY2(!setting.lastAlertToastDate.isValid(), "NULL means the toast has never been shown");
    QVERIFY2(!setting.id.isEmpty(), "the seeded row still needs a real id");
}

void TestAppSettingRepository::theRowIsASingleton()
{
    // The same mechanism migration 001 uses for installation: a second row is
    // refused by the database, not merely avoided by the application.
    QSqlDatabase db = database();

    QSqlQuery insert(db);
    const bool ok = insert.exec(QStringLiteral(
        "INSERT INTO app_setting (id, singleton, critical_days, expiring_soon_days,"
        " due_soon_days, created_at, created_by, updated_at, updated_by, is_deleted,"
        " origin_node, revision)"
        " VALUES ('second', 1, 10, 20, 30, '2026-01-01T00:00:00Z', 'TEST',"
        " '2026-01-01T00:00:00Z', 'TEST', 0, 'LOCAL', 1)"));

    QVERIFY2(!ok, "a second settings row must be refused");

    QSqlQuery count(db);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM app_setting")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TestAppSettingRepository::refusesCriticalNotBelowExpiringSoon()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);

    AppSetting setting;
    QVERIFY(repository.read(&setting));

    setting.criticalDays     = 60;
    setting.expiringSoonDays = 60; // equal is not "strictly increasing"
    setting.dueSoonDays      = 90;

    QVERIFY(!repository.update(setting));
    // The message names the offending pair rather than saying "invalid".
    QVERIFY(repository.errorString().contains(QStringLiteral("Critical")));
    QVERIFY(repository.errorString().contains(QStringLiteral("Expiring Soon")));

    AppSetting stored;
    QVERIFY(repository.read(&stored));
    QCOMPARE(stored.criticalDays, 30); // unchanged
    QCOMPARE(stored.expiringSoonDays, 60);
}

void TestAppSettingRepository::refusesExpiringSoonNotBelowDueSoon()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);

    AppSetting setting;
    QVERIFY(repository.read(&setting));

    setting.criticalDays     = 30;
    setting.expiringSoonDays = 95;
    setting.dueSoonDays      = 90;

    QVERIFY(!repository.update(setting));
    QVERIFY(repository.errorString().contains(QStringLiteral("Expiring Soon")));
    QVERIFY(repository.errorString().contains(QStringLiteral("Due Soon")));

    AppSetting stored;
    QVERIFY(repository.read(&stored));
    QCOMPARE(stored.expiringSoonDays, 60); // unchanged
}

void TestAppSettingRepository::refusesZeroOrNegativeValues_data()
{
    QTest::addColumn<int>("critical");
    QTest::addColumn<int>("expiringSoon");
    QTest::addColumn<int>("dueSoon");

    QTest::newRow("critical zero")      << 0 << 60 << 90;
    QTest::newRow("critical negative")  << -5 << 60 << 90;
    QTest::newRow("expiring zero")      << 30 << 0 << 90;
    QTest::newRow("due soon zero")      << 30 << 60 << 0;
    QTest::newRow("due soon negative")  << 30 << 60 << -1;
}

void TestAppSettingRepository::refusesZeroOrNegativeValues()
{
    QFETCH(int, critical);
    QFETCH(int, expiringSoon);
    QFETCH(int, dueSoon);

    QSqlDatabase         db = database();
    AppSettingRepository repository(db);

    AppSetting setting;
    QVERIFY(repository.read(&setting));
    setting.criticalDays     = critical;
    setting.expiringSoonDays = expiringSoon;
    setting.dueSoonDays      = dueSoon;

    QVERIFY(!repository.update(setting));
    QVERIFY(!repository.errorString().isEmpty());

    AppSetting stored;
    QVERIFY(repository.read(&stored));
    QCOMPARE(stored.criticalDays, 30);
    QCOMPARE(stored.expiringSoonDays, 60);
    QCOMPARE(stored.dueSoonDays, 90);
}

void TestAppSettingRepository::validUpdateIsStoredAndBumpsRevision()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);

    AppSetting setting;
    QVERIFY(repository.read(&setting));
    const int revisionBefore = revisionOf();
    QCOMPARE(revisionBefore, 1);

    setting.criticalDays     = 14;
    setting.expiringSoonDays = 45;
    setting.dueSoonDays      = 75;
    QVERIFY2(repository.update(setting), qPrintable(repository.errorString()));

    AppSetting stored;
    QVERIFY(repository.read(&stored));
    QCOMPARE(stored.criticalDays, 14);
    QCOMPARE(stored.expiringSoonDays, 45);
    QCOMPARE(stored.dueSoonDays, 75);
    QCOMPARE(revisionOf(), revisionBefore + 1);

    // A second save bumps it again rather than sticking.
    stored.dueSoonDays = 80;
    QVERIFY(repository.update(stored));
    QCOMPARE(revisionOf(), revisionBefore + 2);
}

void TestAppSettingRepository::rawCheckConstraintTextNeverReachesTheUser()
{
    // The CHECK constraints are the backstop; the friendly message is the
    // path a user actually sees.
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);

    AppSetting setting;
    QVERIFY(repository.read(&setting));
    setting.criticalDays     = 90;
    setting.expiringSoonDays = 60;
    setting.dueSoonDays      = 30;

    QVERIFY(!repository.update(setting));
    const QString message = repository.errorString();
    QVERIFY2(!message.contains(QStringLiteral("CHECK"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
    QVERIFY2(!message.contains(QStringLiteral("constraint"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
}

void TestAppSettingRepository::updateLeavesTheToastDateAlone()
{
    // Nothing owns last_alert_toast_date until step 9, so saving thresholds
    // must not quietly clear it.
    QSqlDatabase db = database();

    QSqlQuery seed(db);
    QVERIFY(seed.exec(QStringLiteral(
        "UPDATE app_setting SET last_alert_toast_date = '2026-08-01'")));

    AppSettingRepository repository(db);
    AppSetting           setting;
    QVERIFY(repository.read(&setting));
    QCOMPARE(setting.lastAlertToastDate, QDate(2026, 8, 1));

    setting.criticalDays = 20;
    QVERIFY(repository.update(setting));

    AppSetting stored;
    QVERIFY(repository.read(&stored));
    QCOMPARE(stored.lastAlertToastDate, QDate(2026, 8, 1));
}

void TestAppSettingRepository::readReportsAMissingRowRatherThanCrashing()
{
    QSqlDatabase db = database();

    QSqlQuery drop(db);
    QVERIFY(drop.exec(QStringLiteral("DELETE FROM app_setting")));

    AppSettingRepository repository(db);
    AppSetting           setting;
    QVERIFY2(!repository.read(&setting), "a missing row is a failure, not a silent default");
    QVERIFY(repository.errorString().contains(QStringLiteral("missing")));
}

QTEST_MAIN(TestAppSettingRepository)
#include "tst_AppSettingRepository.moc"
