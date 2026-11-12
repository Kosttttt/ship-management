#include "DocumentTestSupport.h"

#include "app/IModule.h"
#include "app/ModuleRegistry.h"
#include "core/DocumentsFolder.h"
#include "core/VesselRepository.h"
#include "modules/documents/DocumentsModule.h"
#include "modules/documents/data/DocumentRepository.h"
#include "modules/documents/service/DocumentFileStore.h"
#include "modules/documents/ui/DocumentListWidget.h"

#include <QLineEdit>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace DocumentTestSupport;

// document-management-spec.md §4.4 and §4.6, the screen: category tabs with
// counts, the search box, and the IModule::setSelectedVessel() hook reaching
// the screen (§3.1).
class TestDocumentListWidget : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theListShowsFleetWideAndTheSelectedVessel();
    void fiveTabsInTheOrderAllSmsCircularsPlansReports();
    void eachTabCarriesItsCount();
    void pickingATabShowsOnlyThatCategory();
    void theSearchBoxFiltersByTitleAndReference();
    void countsFollowTheSearchButNotTheTab();

    void theModuleHookReachesTheScreen();
    void theRegistryRegistersTheDocumentsModule();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static QTabBar* tabsOf(QWidget& widget)
    {
        return widget.findChild<QTabBar*>(QStringLiteral("documentCategoryTabs"));
    }
    static QTableWidget* tableOf(QWidget& widget)
    {
        return widget.findChild<QTableWidget*>(QStringLiteral("documentTable"));
    }
    static QLineEdit* searchOf(QWidget& widget)
    {
        return widget.findChild<QLineEdit*>(QStringLiteral("documentSearchEdit"));
    }
    static int indexOf(QTabBar* tabs, DocumentCategory category)
    {
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabData(index).toInt() == static_cast<int>(category)) {
                return index;
            }
        }
        return -1;
    }

    void seedLibrary();

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_settingsFolder;
};

void TestDocumentListWidget::init()
{
    m_connectionName = QStringLiteral("doclist_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    m_vesselId      = seedVessel(db, QStringLiteral("v1"), QStringLiteral("MV Alpha"),
                                 QStringLiteral("9074729"));
    QVERIFY(seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Beta"),
                       QStringLiteral("9074730"))
                .isEmpty()
            == false);

    // Never the developer's own settings (CLAUDE.md §10.1): building the
    // screen reads the documents folder.
    QVERIFY(m_settingsFolder.isValid());
    DocumentsFolder::setSettingsFileOverride(
        m_settingsFolder.filePath(QStringLiteral("%1.ini").arg(QTest::currentTestFunction())));
}

void TestDocumentListWidget::cleanup()
{
    closeDatabase(m_connectionName);
    DocumentsFolder::setSettingsFileOverride(QString());
}

void TestDocumentListWidget::seedLibrary()
{
    QSqlDatabase       db = database();
    DocumentRepository office(db, officeContext());

    // Two SMS (one fleet, one this vessel's), one circular, one plan, no
    // reports — so an empty tab is covered too.
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("SMS-01"),
                                         QStringLiteral("Company Policy")),
                          sampleVersion(QStringLiteral("Fleet/SMS/a.pdf"))));
    QVERIFY(office.create(sampleDocument(m_vesselId, QStringLiteral("SMS-02"),
                                         QStringLiteral("Shipboard Procedures")),
                          sampleVersion(QStringLiteral("9074729/SMS/b.pdf"))));
    QVERIFY(office.create(sampleDocument(QString(), QStringLiteral("CIR-12"),
                                         QStringLiteral("Fleet Circular 12/2026"),
                                         DocumentCategory::Circular),
                          sampleVersion(QStringLiteral("Fleet/CIRCULAR/c.pdf"))));
    QVERIFY(office.create(sampleDocument(m_vesselId, QStringLiteral("PLAN-1"),
                                         QStringLiteral("General Arrangement"),
                                         DocumentCategory::PlanManual),
                          sampleVersion(QStringLiteral("9074729/PLAN_MANUAL/d.pdf"))));
    // Another vessel's, which must never appear.
    QVERIFY(office.create(sampleDocument(QStringLiteral("v2"), QStringLiteral("PLAN-9"),
                                         QStringLiteral("Beta Arrangement"),
                                         DocumentCategory::PlanManual),
                          sampleVersion(QStringLiteral("9074730/PLAN_MANUAL/e.pdf"))));
}

// Everything the screen needs, built the same way in every test. A macro
// because DocumentListWidget takes references that must outlive it.
#define BUILD_SCREEN(name)                                                                     \
    QSqlDatabase       db = database();                                                        \
    DocumentRepository repository(db, officeContext());                                        \
    VesselRepository   vessels(db, officeContext());                                           \
    DocumentFileStore  files(vessels);                                                         \
    DocumentListWidget name(repository, files, vessels, officeContext());                      \
    name.setVesselId(m_vesselId)

void TestDocumentListWidget::theListShowsFleetWideAndTheSelectedVessel()
{
    seedLibrary();
    BUILD_SCREEN(screen);

    // Four: two fleet-wide and two of this vessel's. Not the fifth, which
    // belongs to MV Beta.
    QCOMPARE(tableOf(screen)->rowCount(), 4);
}

void TestDocumentListWidget::fiveTabsInTheOrderAllSmsCircularsPlansReports()
{
    seedLibrary();
    BUILD_SCREEN(screen);

    QTabBar* tabs = tabsOf(screen);
    QVERIFY2(tabs != nullptr, "the documents list must have a category bar");
    QCOMPARE(tabs->count(), 5);

    const QList<DocumentCategory> expected{DocumentCategory::Unset, DocumentCategory::Sms,
                                           DocumentCategory::Circular,
                                           DocumentCategory::PlanManual,
                                           DocumentCategory::Report};
    for (int index = 0; index < expected.size(); ++index) {
        QCOMPARE(tabs->tabData(index).toInt(), static_cast<int>(expected.at(index)));
    }

    // Opens on All (§4.4).
    QCOMPARE(tabs->currentIndex(), 0);
}

void TestDocumentListWidget::eachTabCarriesItsCount()
{
    seedLibrary();
    BUILD_SCREEN(screen);

    QTabBar* tabs = tabsOf(screen);
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Unset)), QStringLiteral("All (4)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Sms)), QStringLiteral("SMS (2)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Circular)),
             QStringLiteral("Circulars (1)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Report)),
             QStringLiteral("Reports (0)"));
}

void TestDocumentListWidget::pickingATabShowsOnlyThatCategory()
{
    seedLibrary();
    BUILD_SCREEN(screen);

    QTabBar* tabs = tabsOf(screen);

    tabs->setCurrentIndex(indexOf(tabs, DocumentCategory::Sms));
    QCOMPARE(tableOf(screen)->rowCount(), 2);

    tabs->setCurrentIndex(indexOf(tabs, DocumentCategory::Report));
    QCOMPARE(tableOf(screen)->rowCount(), 0);

    tabs->setCurrentIndex(indexOf(tabs, DocumentCategory::Unset));
    QCOMPARE(tableOf(screen)->rowCount(), 4);
}

void TestDocumentListWidget::theSearchBoxFiltersByTitleAndReference()
{
    seedLibrary();
    BUILD_SCREEN(screen);

    QLineEdit* search = searchOf(screen);
    QVERIFY(search != nullptr);

    // By title…
    search->setText(QStringLiteral("Arrangement"));
    QCOMPARE(tableOf(screen)->rowCount(), 1);

    // …and by reference, which is what people actually call these.
    search->setText(QStringLiteral("SMS-0"));
    QCOMPARE(tableOf(screen)->rowCount(), 2);

    // Case does not matter: nobody types a reference the way it was entered.
    search->setText(QStringLiteral("company policy"));
    QCOMPARE(tableOf(screen)->rowCount(), 1);

    search->clear();
    QCOMPARE(tableOf(screen)->rowCount(), 4);
}

void TestDocumentListWidget::countsFollowTheSearchButNotTheTab()
{
    seedLibrary();
    BUILD_SCREEN(screen);

    QTabBar* tabs = tabsOf(screen);

    searchOf(screen)->setText(QStringLiteral("SMS-0"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Unset)), QStringLiteral("All (2)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Sms)), QStringLiteral("SMS (2)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Circular)),
             QStringLiteral("Circulars (0)"));

    // Standing on one tab must not make the others read zero: each shows its
    // own number whichever one is open.
    searchOf(screen)->clear();
    tabs->setCurrentIndex(indexOf(tabs, DocumentCategory::Circular));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Sms)), QStringLiteral("SMS (2)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, DocumentCategory::Unset)), QStringLiteral("All (4)"));
}

void TestDocumentListWidget::theModuleHookReachesTheScreen()
{
    seedLibrary();

    QSqlDatabase   db = database();
    DocumentsModule module(db, officeContext());

    QWidget parent;
    auto*   screen = qobject_cast<DocumentListWidget*>(module.createMainWidget(&parent));
    QVERIFY(screen != nullptr);

    // §3.1: the shell calls setSelectedVessel() on every module and never
    // learns any module's screen type. Here is the other end of that.
    QCOMPARE(tableOf(*screen)->rowCount(), 2); // fleet-wide only, nothing selected yet

    module.setSelectedVessel(m_vesselId);
    QCOMPARE(screen->vesselId(), m_vesselId);
    QCOMPARE(tableOf(*screen)->rowCount(), 4); // plus this vessel's two

    // And back to nothing, the way the toolbar's placeholder leaves it.
    module.setSelectedVessel(QString());
    QCOMPARE(tableOf(*screen)->rowCount(), 2);
}

void TestDocumentListWidget::theRegistryRegistersTheDocumentsModule()
{
    QSqlDatabase   db = database();
    ModuleRegistry registry(db, officeContext());

    // One line in ModuleRegistry is all it took (CLAUDE.md §7), and the
    // module contributes no migrations of its own — migrations/ stays one
    // flat, globally numbered folder.
    IModule* documents = nullptr;
    for (IModule* module : registry.modules()) {
        if (module->id() == QLatin1String("documents")) {
            documents = module;
        }
    }
    QVERIFY(documents != nullptr);
    QCOMPARE(documents->displayName(), QStringLiteral("Documents"));
    QVERIFY(documents->migrations().isEmpty());

    // Nothing to alert about: this module has no dates that fall due.
    QVERIFY(documents->alertProviders().isEmpty());
}

QTEST_MAIN(TestDocumentListWidget)
#include "tst_DocumentListWidget.moc"
