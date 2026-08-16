#include "app/AlertBanner.h"
#include "app/AlertProvider.h"
#include "app/IModule.h"
#include "app/MainWindow.h"
#include "app/ModuleRegistry.h"
#include "core/AppSettingRepository.h"
#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "core/VesselRepository.h"
#include "modules/certificates/CertificateAlertProvider.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

// alerts-spec.md §9, the nine edge cases.
//
// Dates are anchored to QDate::currentDate() because the provider reads the
// clock itself (§4 step 3), so each expected count stays fixed on any run day.
class TestAlerts : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void nothingOutstandingMeansNoBannerAndNoBadge();              // 1
    void oneVesselOutstandingInOfficeMode();                       // 2
    void viewSwitchesTheSelectorAndAppliesTheFilter();             // 2
    void multipleVesselsEachGetTheirOwnRow();                      // 3
    void viewOnOneVesselLeavesTheOtherRowsAlone();                 // 3
    void vesselModeShowsOnlyItsOwnVessel();                        // 4
    void vesselModeViewAppliesTheFilterWithNoSelector();           // 4
    void bannerStaysHiddenWhenAlreadyShownToday();                 // 5
    void badgeStillCountsOnADayTheBannerStaysSilent();             // 5
    void nothingOutstandingLeavesTheToastDateUntouched();          // 6
    void showingTheBannerRecordsTheDateImmediately();              // 7
    void unreadableThresholdsStillProduceCounts();                 // 8
    void badgeFollowsCertificatesChangedWithoutARestart();         // 9

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static InstallationContext officeContext()
    {
        InstallationRecord record;
        record.id     = QStringLiteral("install-office");
        record.mode   = InstallationMode::Office;
        record.nodeId = QStringLiteral("OFFICE");
        return InstallationContext(record);
    }

    static InstallationContext vesselContext(const QString& vesselId)
    {
        InstallationRecord record;
        record.id              = QStringLiteral("install-vessel");
        record.mode            = InstallationMode::Vessel;
        record.nodeId          = QStringLiteral("VESSEL-9074729");
        record.vesselId        = vesselId;
        record.vesselName      = QStringLiteral("MV Aurora");
        record.vesselImoNumber = QStringLiteral("9074729");
        return InstallationContext(record);
    }

    QString seedVessel(const QString& id, const QString& name, const QString& imo);

    // An expired certificate: DisplayStatus::Expired, so it always counts.
    void seedOutstandingCertificate(const QString& id, const QString& vesselId);
    // A healthy one, five years to run and no survey required.
    void seedHealthyCertificate(const QString& id, const QString& vesselId);

    static AlertBanner*  bannerOf(MainWindow& window);
    static QListWidget*  sidebarOf(MainWindow& window);
    static QList<QPushButton*> viewButtonsOf(AlertBanner& banner);
    static QString       certificatesRowText(MainWindow& window);

    QString m_connectionName;
};

void TestAlerts::init()
{
    m_connectionName = QStringLiteral("alerts_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
}

void TestAlerts::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString TestAlerts::seedVessel(const QString& id, const QString& name, const QString& imo)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
        " updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
        " 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(imo);
    query.exec();
    return id;
}

void TestAlerts::seedOutstandingCertificate(const QString& id, const QString& vesselId)
{
    const QDate expiry = QDate::currentDate().addDays(-10); // expired
    QSqlQuery   query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " requires_annual_survey, requires_intermediate_survey,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, 'Lapsed Certificate', 'STATUTORY', ?, ?, 0, 0,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(vesselId);
    query.addBindValue(expiry.addYears(-5).toString(Qt::ISODate));
    query.addBindValue(expiry.toString(Qt::ISODate));
    QVERIFY(query.exec());
}

void TestAlerts::seedHealthyCertificate(const QString& id, const QString& vesselId)
{
    const QDate today = QDate::currentDate();
    QSqlQuery   query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " requires_annual_survey, requires_intermediate_survey,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, 'Healthy Certificate', 'STATUTORY', ?, ?, 0, 0,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(vesselId);
    query.addBindValue(today.toString(Qt::ISODate));
    query.addBindValue(today.addYears(5).toString(Qt::ISODate));
    QVERIFY(query.exec());
}

AlertBanner* TestAlerts::bannerOf(MainWindow& window)
{
    return window.findChild<AlertBanner*>(QStringLiteral("alertBanner"));
}

QListWidget* TestAlerts::sidebarOf(MainWindow& window)
{
    return window.findChild<QListWidget*>(QStringLiteral("navigationSidebar"));
}

QList<QPushButton*> TestAlerts::viewButtonsOf(AlertBanner& banner)
{
    QList<QPushButton*> buttons;
    for (QPushButton* button : banner.findChildren<QPushButton*>()) {
        if (button->objectName() == QLatin1String("alertBannerView")) {
            buttons.append(button);
        }
    }
    return buttons;
}

QString TestAlerts::certificatesRowText(MainWindow& window)
{
    QListWidget* sidebar = sidebarOf(window);
    // Row 0 is Vessels, the last row is Settings; the module sits between.
    return sidebar == nullptr ? QString() : sidebar->item(1)->text();
}

void TestAlerts::nothingOutstandingMeansNoBannerAndNoBadge()
{
    // §9 item 1.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedHealthyCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QVERIFY2(!bannerOf(window)->isVisibleTo(&window), "nothing outstanding, so no banner");
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates"));
}

void TestAlerts::oneVesselOutstandingInOfficeMode()
{
    // §9 item 2.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QVERIFY(bannerOf(window)->isVisibleTo(&window));
    QCOMPARE(viewButtonsOf(*bannerOf(window)).size(), 1);
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates (1)"));
}

void TestAlerts::viewSwitchesTheSelectorAndAppliesTheFilter()
{
    // §9 item 2, the drill-down half.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(QStringLiteral("v2"), QStringLiteral("MV Bravo"), QStringLiteral("9319466"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v2"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    auto* selector = window.findChild<QComboBox*>(QStringLiteral("vesselSelector"));
    QVERIFY(selector != nullptr);
    QCOMPARE(selector->currentIndex(), -1); // nothing chosen yet

    viewButtonsOf(*bannerOf(window)).first()->click();

    // The selector now points at the vessel the banner named...
    QCOMPARE(selector->currentData().toString(), QStringLiteral("v2"));

    // ...the filter is on, and the certificates page is showing.
    auto* filter = window.findChild<QCheckBox*>(QStringLiteral("needsAttentionCheck"));
    QVERIFY(filter != nullptr);
    QVERIFY(filter->isChecked());
    QCOMPARE(sidebarOf(window)->currentRow(), 1);
}

void TestAlerts::multipleVesselsEachGetTheirOwnRow()
{
    // §9 item 3.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(QStringLiteral("v2"), QStringLiteral("MV Bravo"), QStringLiteral("9319466"));
    seedVessel(QStringLiteral("v3"), QStringLiteral("MV Charlie"), QStringLiteral("9247455"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));
    seedOutstandingCertificate(QStringLiteral("c2"), QStringLiteral("v2"));
    seedHealthyCertificate(QStringLiteral("c3"), QStringLiteral("v3")); // no row for this one

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QCOMPARE(viewButtonsOf(*bannerOf(window)).size(), 2);
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates (2)"));
}

void TestAlerts::viewOnOneVesselLeavesTheOtherRowsAlone()
{
    // §9 item 3: acting on one row must not disturb the others.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(QStringLiteral("v2"), QStringLiteral("MV Bravo"), QStringLiteral("9319466"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));
    seedOutstandingCertificate(QStringLiteral("c2"), QStringLiteral("v2"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);
    AlertBanner* banner = bannerOf(window);

    QCOMPARE(viewButtonsOf(*banner).size(), 2);
    viewButtonsOf(*banner).first()->click();

    QVERIFY2(banner->isVisibleTo(&window), "the banner stays up after a drill-down");
    QCOMPARE(viewButtonsOf(*banner).size(), 2);
}

void TestAlerts::vesselModeShowsOnlyItsOwnVessel()
{
    // §9 item 4. A second vessel's row exists in the database, as a future
    // sync might leave behind; the provider must not see it.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Aurora"), QStringLiteral("9074729"));
    seedVessel(QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9319466"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));
    seedOutstandingCertificate(QStringLiteral("c2"), QStringLiteral("v2"));

    QSqlDatabase              db      = database();
    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QCOMPARE(viewButtonsOf(*bannerOf(window)).size(), 1);
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates (1)"));
}

void TestAlerts::vesselModeViewAppliesTheFilterWithNoSelector()
{
    // §9 item 4, the drill-down half: there is no selector to touch.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Aurora"), QStringLiteral("9074729"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase              db      = database();
    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QVERIFY(window.findChild<QComboBox*>(QStringLiteral("vesselSelector")) == nullptr);

    viewButtonsOf(*bannerOf(window)).first()->click();

    auto* filter = window.findChild<QCheckBox*>(QStringLiteral("needsAttentionCheck"));
    QVERIFY(filter != nullptr);
    QVERIFY(filter->isChecked());
    QCOMPARE(sidebarOf(window)->currentRow(), 1);
}

void TestAlerts::bannerStaysHiddenWhenAlreadyShownToday()
{
    // §9 item 5.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase db = database();
    QSqlQuery    already(db);
    already.prepare(QStringLiteral("UPDATE app_setting SET last_alert_toast_date = ?"));
    already.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    QVERIFY(already.exec());

    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QVERIFY2(!bannerOf(window)->isVisibleTo(&window), "already shown today");
}

void TestAlerts::badgeStillCountsOnADayTheBannerStaysSilent()
{
    // §9 item 5: the badge is independent of the once-a-day rule.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase db = database();
    QSqlQuery    already(db);
    already.prepare(QStringLiteral("UPDATE app_setting SET last_alert_toast_date = ?"));
    already.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    QVERIFY(already.exec());

    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);

    QVERIFY(!bannerOf(window)->isVisibleTo(&window));
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates (1)"));
}

void TestAlerts::nothingOutstandingLeavesTheToastDateUntouched()
{
    // §9 item 6: it was not actually shown, so tomorrow it is still eligible.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedHealthyCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase db = database();
    QSqlQuery    yesterday(db);
    yesterday.prepare(QStringLiteral("UPDATE app_setting SET last_alert_toast_date = ?"));
    yesterday.addBindValue(QDate::currentDate().addDays(-1).toString(Qt::ISODate));
    QVERIFY(yesterday.exec());

    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);
    QVERIFY(!bannerOf(window)->isVisibleTo(&window));

    AppSetting stored;
    QVERIFY(appSettings.read(&stored));
    QCOMPARE(stored.lastAlertToastDate, QDate::currentDate().addDays(-1));
}

void TestAlerts::showingTheBannerRecordsTheDateImmediately()
{
    // §9 item 7: recorded when populated, not on dismiss, so dismissing
    // without reading it still counts as shown for the day.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);
    QVERIFY(bannerOf(window)->isVisibleTo(&window));

    AppSetting stored;
    QVERIFY(appSettings.read(&stored));
    QCOMPARE(stored.lastAlertToastDate, QDate::currentDate());

    // A second window in the same session no longer shows it.
    MainWindow second(context, vessels, appSettings, registry);
    QVERIFY2(!bannerOf(second)->isVisibleTo(&second), "already recorded as shown today");
}

void TestAlerts::unreadableThresholdsStillProduceCounts()
{
    // §9 item 8: the provider falls back to the hardcoded 30/60/90 defaults
    // rather than skipping the computation.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    QSqlDatabase db = database();
    QSqlQuery    wipe(db);
    QVERIFY(wipe.exec(QStringLiteral("DELETE FROM app_setting")));

    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);
    AppSettingRepository      appSettings(db);

    AppSetting unused;
    QVERIFY2(!appSettings.read(&unused), "the settings row really is gone");

    CertificateAlertProvider provider(vessels, certificates, endorsements, appSettings, context);
    const QList<VesselAttentionCount> counts = provider.attentionByVessel();

    QCOMPARE(counts.size(), 1);
    QCOMPARE(counts.first().vesselId, QStringLiteral("v1"));
    QCOMPARE(counts.first().count, 1);
}

void TestAlerts::badgeFollowsCertificatesChangedWithoutARestart()
{
    // §9 item 9: the badge is live within a session. The banner deliberately
    // is not — it is a startup snapshot.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    AppSettingRepository      appSettings(db);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, appSettings, registry);
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates"));

    // Something becomes outstanding while the window is open.
    seedOutstandingCertificate(QStringLiteral("c1"), QStringLiteral("v1"));

    auto* list = window.findChild<CertificateListWidget*>();
    QVERIFY(list != nullptr);
    QSignalSpy spy(list, &CertificateListWidget::certificatesChanged);

    // Any reload emits the signal the badge follows.
    list->setVesselId(QStringLiteral("v1"));

    QVERIFY(spy.count() > 0);
    QCOMPARE(certificatesRowText(window), QStringLiteral("Certificates (1)"));
}

QTEST_MAIN(TestAlerts)
#include "tst_Alerts.moc"
