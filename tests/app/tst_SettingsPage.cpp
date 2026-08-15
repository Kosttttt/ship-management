#include "app/SettingsPage.h"

#include "core/AppSettingRepository.h"
#include "core/MigrationRunner.h"

#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QtTest>

// settings-app-setting-spec §8 item 7: the Save button follows the ordering
// rule live, so the invalid combination is never something a user can submit.
class TestSettingsPage : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void loadsTheStoredValues();
    void saveIsDisabledWhenCriticalReachesExpiringSoon();
    void saveIsDisabledWhenExpiringSoonReachesDueSoon();
    void saveReEnablesOnceTheOrderIsCorrected();
    void theHintNamesTheOffendingPair();
    void spinBoxesRefuseZeroAndNegativeValues();
    void savingWritesThroughToTheRepository();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static QSpinBox* critical(SettingsPage& page)
    {
        return page.findChild<QSpinBox*>(QStringLiteral("criticalSpin"));
    }
    static QSpinBox* expiringSoon(SettingsPage& page)
    {
        return page.findChild<QSpinBox*>(QStringLiteral("expiringSoonSpin"));
    }
    static QSpinBox* dueSoon(SettingsPage& page)
    {
        return page.findChild<QSpinBox*>(QStringLiteral("dueSoonSpin"));
    }
    static QPushButton* saveButton(SettingsPage& page)
    {
        return page.findChild<QPushButton*>(QStringLiteral("saveSettingsButton"));
    }
    static QLabel* hint(SettingsPage& page)
    {
        return page.findChild<QLabel*>(QStringLiteral("thresholdHint"));
    }

    QString m_connectionName;
};

void TestSettingsPage::init()
{
    m_connectionName = QStringLiteral("settingspage_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
}

void TestSettingsPage::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

void TestSettingsPage::loadsTheStoredValues()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    QCOMPARE(critical(page)->value(), 30);
    QCOMPARE(expiringSoon(page)->value(), 60);
    QCOMPARE(dueSoon(page)->value(), 90);
    QVERIFY(saveButton(page)->isEnabled());
    QVERIFY(hint(page)->text().isEmpty());
}

void TestSettingsPage::saveIsDisabledWhenCriticalReachesExpiringSoon()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    critical(page)->setValue(60); // equal to expiring soon
    QVERIFY2(!saveButton(page)->isEnabled(), "equal is not strictly increasing");

    critical(page)->setValue(75); // past it
    QVERIFY(!saveButton(page)->isEnabled());
}

void TestSettingsPage::saveIsDisabledWhenExpiringSoonReachesDueSoon()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    expiringSoon(page)->setValue(90);
    QVERIFY(!saveButton(page)->isEnabled());

    expiringSoon(page)->setValue(120);
    QVERIFY(!saveButton(page)->isEnabled());
}

void TestSettingsPage::saveReEnablesOnceTheOrderIsCorrected()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    critical(page)->setValue(80);
    QVERIFY(!saveButton(page)->isEnabled());

    critical(page)->setValue(20);
    QVERIFY2(saveButton(page)->isEnabled(), "correcting the order must re-enable Save");
    QVERIFY(hint(page)->text().isEmpty());
}

void TestSettingsPage::theHintNamesTheOffendingPair()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    critical(page)->setValue(65);
    QVERIFY(hint(page)->text().contains(QStringLiteral("Critical"), Qt::CaseInsensitive));
    QVERIFY(hint(page)->text().contains(QStringLiteral("Expiring"), Qt::CaseInsensitive));

    // Put that pair back in order and break the other one instead.
    critical(page)->setValue(20);
    expiringSoon(page)->setValue(95);
    QVERIFY(hint(page)->text().contains(QStringLiteral("Expiring"), Qt::CaseInsensitive));
    QVERIFY(hint(page)->text().contains(QStringLiteral("Due"), Qt::CaseInsensitive));
}

void TestSettingsPage::spinBoxesRefuseZeroAndNegativeValues()
{
    // A day count below one is meaningless, so the widget cannot hold one —
    // the same "the widget enforces the rule" approach as gross tonnage.
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    critical(page)->setValue(0);
    QCOMPARE(critical(page)->value(), 1);

    critical(page)->setValue(-10);
    QCOMPARE(critical(page)->value(), 1);
}

void TestSettingsPage::savingWritesThroughToTheRepository()
{
    QSqlDatabase         db = database();
    AppSettingRepository repository(db);
    SettingsPage         page(repository);

    critical(page)->setValue(14);
    expiringSoon(page)->setValue(45);
    dueSoon(page)->setValue(75);
    QVERIFY(saveButton(page)->isEnabled());

    saveButton(page)->click();

    AppSetting stored;
    QVERIFY(repository.read(&stored));
    QCOMPARE(stored.criticalDays, 14);
    QCOMPARE(stored.expiringSoonDays, 45);
    QCOMPARE(stored.dueSoonDays, 75);
}

QTEST_MAIN(TestSettingsPage)
#include "tst_SettingsPage.moc"
