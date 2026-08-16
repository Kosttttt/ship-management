#include "app/IModule.h"
#include "app/MainWindow.h"
#include "app/ModuleRegistry.h"
#include "core/AppSettingRepository.h"
#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "core/VesselRepository.h"

#include <QComboBox>
#include <QListWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

// certificate-crud-spec §7 item 10: the registry exposes exactly one module,
// and the sidebar shows "Vessels" plus that module's displayName().
class TestModuleRegistry : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registryExposesExactlyOneModule();
    void theModuleIdentifiesItself();
    void theModuleContributesNoMigrationsOrAlertsYet();
    void sidebarShowsVesselsPlusEveryModule();
    void officeModeHasAVesselSelectorStartingUnselected();
    void vesselModeHasNoVesselSelector();
    void settingsEntryIsPresentInVesselModeToo();

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
        record.vesselName      = QStringLiteral("MV Example");
        record.vesselImoNumber = QStringLiteral("9074729");
        return InstallationContext(record);
    }

    QString seedVessel(const QString& id, const QString& name, const QString& imo);

    QString m_connectionName;
};

void TestModuleRegistry::init()
{
    m_connectionName = QStringLiteral("registry_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
}

void TestModuleRegistry::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString TestModuleRegistry::seedVessel(const QString& id, const QString& name, const QString& imo)
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

void TestModuleRegistry::registryExposesExactlyOneModule()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    ModuleRegistry            registry(db, context);

    QCOMPARE(registry.modules().size(), 1);
}

void TestModuleRegistry::theModuleIdentifiesItself()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    ModuleRegistry            registry(db, context);

    IModule* module = registry.modules().first();
    QCOMPARE(module->id(), QStringLiteral("certificates"));
    QCOMPARE(module->displayName(), QStringLiteral("Certificates"));
}

void TestModuleRegistry::theModuleContributesNoMigrationsOrAlertsYet()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    ModuleRegistry            registry(db, context);

    IModule* module = registry.modules().first();

    // migrations/ stays one flat, globally numbered folder, so a module still
    // contributes none of its own (certificate-crud-spec §3).
    QVERIFY(module->migrations().isEmpty());

    // alertProviders() was empty until step 9; it now reports exactly one,
    // and the pointer is non-owning — the module owns the instance
    // (alerts-spec.md §3).
    QCOMPARE(module->alertProviders().size(), 1);
    QVERIFY(module->alertProviders().first() != nullptr);

    // Asking twice returns the same instance rather than building a new one.
    QCOMPARE(module->alertProviders().first(), module->alertProviders().first());
}

void TestModuleRegistry::sidebarShowsVesselsPlusEveryModule()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    ModuleRegistry            registry(db, context);

    AppSettingRepository appSettings(db);
    MainWindow window(context, vessels, appSettings, registry);

    auto* sidebar = window.findChild<QListWidget*>(QStringLiteral("navigationSidebar"));
    QVERIFY(sidebar != nullptr);

    // Vessels first, then one row per module, then Settings — both of the
    // fixed entries are core rather than modules (settings-app-setting-spec §6).
    QCOMPARE(sidebar->count(), 3);
    QCOMPARE(sidebar->item(0)->text(), QStringLiteral("Vessels"));
    QCOMPARE(sidebar->item(1)->text(), registry.modules().first()->displayName());
    QCOMPARE(sidebar->item(2)->text(), QStringLiteral("Settings"));

    // Vessels is core, not a module, so it is always first and always present.
    QCOMPARE(sidebar->currentRow(), 0);
}

void TestModuleRegistry::officeModeHasAVesselSelectorStartingUnselected()
{
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(QStringLiteral("v2"), QStringLiteral("MV Bravo"), QStringLiteral("9319466"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    ModuleRegistry            registry(db, context);

    AppSettingRepository appSettings(db);
    MainWindow window(context, vessels, appSettings, registry);

    auto* selector = window.findChild<QComboBox*>(QStringLiteral("vesselSelector"));
    QVERIFY2(selector != nullptr, "the office installation needs a ship selector");
    QCOMPARE(selector->count(), 2);
    QCOMPARE(selector->currentIndex(), -1); // nothing preselected, on purpose
}

void TestModuleRegistry::vesselModeHasNoVesselSelector()
{
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Example"), QStringLiteral("9074729"));

    QSqlDatabase              db      = database();
    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    VesselRepository          vessels(db, context);
    ModuleRegistry            registry(db, context);

    AppSettingRepository appSettings(db);
    MainWindow window(context, vessels, appSettings, registry);

    // CLAUDE.md §3: the ship selector is hidden on a vessel installation.
    QVERIFY(window.findChild<QComboBox*>(QStringLiteral("vesselSelector")) == nullptr);
}

void TestModuleRegistry::settingsEntryIsPresentInVesselModeToo()
{
    // settings-app-setting-spec §6: visible in both installation modes, with
    // no role gating — roles are layered on at the end of the project.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Example"), QStringLiteral("9074729"));

    QSqlDatabase              db      = database();
    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    VesselRepository          vessels(db, context);
    ModuleRegistry            registry(db, context);

    AppSettingRepository appSettings(db);
    MainWindow           window(context, vessels, appSettings, registry);

    auto* sidebar = window.findChild<QListWidget*>(QStringLiteral("navigationSidebar"));
    QVERIFY(sidebar != nullptr);
    QCOMPARE(sidebar->count(), 3);
    QCOMPARE(sidebar->item(2)->text(), QStringLiteral("Settings"));
}

QTEST_MAIN(TestModuleRegistry)
#include "tst_ModuleRegistry.moc"
