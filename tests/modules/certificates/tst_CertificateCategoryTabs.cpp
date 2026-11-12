#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateListRows.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QCheckBox>
#include <QTabBar>
#include <QTableWidget>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-category-tabs-spec §5, the widget half: the tabs are there and
// in the right order, the screen opens on All, picking a tab filters the
// table, changing tab clears the selection, and the banner's drill-down comes
// back to All.
//
// What the counts and colours *are* is tst_CertificateListRows' job — this
// suite only checks that the bar shows and acts on them.
class TestCertificateCategoryTabs : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void fiveTabsInTheOrderAllStatutoryClassEquipmentOther();
    void theScreenOpensOnAll();
    void eachLabelCarriesItsCount();
    void aTabWithSomethingNeedingAttentionIsRed();

    void pickingATabShowsOnlyThatCategory();
    void changingTabClearsTheSelection();
    void theDrillDownComesBackToAll();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static QTabBar* tabsOf(CertificateListWidget& widget)
    {
        return widget.findChild<QTabBar*>(QStringLiteral("certificateCategoryTabs"));
    }
    static QTableWidget* tableOf(CertificateListWidget& widget)
    {
        return widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    }
    static QCheckBox* attentionCheckOf(CertificateListWidget& widget)
    {
        return widget.findChild<QCheckBox*>(QStringLiteral("needsAttentionCheck"));
    }

    // The index of a tab by the category it carries, so no test depends on
    // counting positions.
    static int indexOf(QTabBar* tabs, CertificateCategory category)
    {
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabData(index).toInt() == static_cast<int>(category)) {
                return index;
            }
        }
        return -1;
    }

    QString m_connectionName;
};

void TestCertificateCategoryTabs::init()
{
    m_connectionName = QStringLiteral("certtabs_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();

    const QDate issued = QDate(2026, 1, 1);
    const QDate safe   = QDate(2031, 1, 1);

    // Two statutory, one class, one equipment, nothing "other". The statutory
    // pair includes one that expired in 2020, so exactly one category needs
    // attention whatever today is.
    seedCategorisedCertificate(db, QStringLiteral("s1"), QStringLiteral("v1"),
                               QStringLiteral("Safety Construction"),
                               QStringLiteral("STATUTORY"), issued, safe);
    seedCategorisedCertificate(db, QStringLiteral("s2"), QStringLiteral("v1"),
                               QStringLiteral("Load Line"), QStringLiteral("STATUTORY"),
                               QDate(2015, 1, 1), QDate(2020, 1, 1));
    seedCategorisedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"),
                               QStringLiteral("Class Hull"), QStringLiteral("CLASS"), issued,
                               safe);
    seedCategorisedCertificate(db, QStringLiteral("e1"), QStringLiteral("v1"),
                               QStringLiteral("Liferaft Servicing"),
                               QStringLiteral("EQUIPMENT"), issued, safe);
}

void TestCertificateCategoryTabs::cleanup()
{
    closeDatabase(m_connectionName);
}

// Everything the widget needs, built the same way in every test. A macro
// rather than a helper function because CertificateListWidget takes nine
// references that must outlive it, and returning one from a function would
// mean managing that lifetime by hand in every test.
#define BUILD_WIDGET(name)                                                                     \
    QSqlDatabase              db      = database();                                            \
    const InstallationContext context = officeContext();                                       \
    CertificateRepository     repository(db, context);                                         \
    EndorsementRepository     endorsements(db, context);                                       \
    CertificateExtensionRepository extensions(db, context);                                    \
    AppSettingRepository           appSettings(db);                                            \
    AttachmentFixture              attachments(db, context);                                   \
    CertificateListWidget          name(repository, endorsements, extensions,                  \
                                        attachments.service, appSettings,                      \
                                        attachments.csvExporter, attachments.csvImporter,      \
                                        attachments.reportBuilder, context);                   \
    name.setVesselId(QStringLiteral("v1"))

void TestCertificateCategoryTabs::fiveTabsInTheOrderAllStatutoryClassEquipmentOther()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);
    QVERIFY2(tabs != nullptr, "the list must have a category bar");
    QCOMPARE(tabs->count(), 5);

    // §3's order, read off the tabs themselves rather than their text, which
    // also carries the counts.
    const QList<CertificateCategory> expected{
        CertificateCategory::Unset, CertificateCategory::Statutory, CertificateCategory::Class,
        CertificateCategory::Equipment, CertificateCategory::Other};
    for (int index = 0; index < expected.size(); ++index) {
        QCOMPARE(tabs->tabData(index).toInt(), static_cast<int>(expected.at(index)));
    }

    QVERIFY2(tabs->tabText(0).startsWith(QStringLiteral("All")), qPrintable(tabs->tabText(0)));
    QVERIFY2(tabs->tabText(4).startsWith(QStringLiteral("Other")), qPrintable(tabs->tabText(4)));
}

void TestCertificateCategoryTabs::theScreenOpensOnAll()
{
    BUILD_WIDGET(widget);

    // §3: deliberately not remembered between runs — the first thing seen at
    // start-up must never hide an expired certificate behind another tab.
    QCOMPARE(tabsOf(widget)->currentIndex(), 0);
    QCOMPARE(tableOf(widget)->rowCount(), 4);
}

void TestCertificateCategoryTabs::eachLabelCarriesItsCount()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);
    QCOMPARE(tabs->tabText(indexOf(tabs, CertificateCategory::Unset)),
             QStringLiteral("All (4)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, CertificateCategory::Statutory)),
             QStringLiteral("Statutory (2)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, CertificateCategory::Class)),
             QStringLiteral("Class (1)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, CertificateCategory::Equipment)),
             QStringLiteral("Equipment (1)"));
    QCOMPARE(tabs->tabText(indexOf(tabs, CertificateCategory::Other)),
             QStringLiteral("Other (0)"));
}

void TestCertificateCategoryTabs::aTabWithSomethingNeedingAttentionIsRed()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);

    // §2: the expired Load Line is statutory, so that tab — and All — are
    // red, and the categories holding only valid certificates are not. The
    // value of the colour is entirely in not being on everything.
    QCOMPARE(tabs->tabTextColor(indexOf(tabs, CertificateCategory::Statutory)), QColor(Qt::red));
    QCOMPARE(tabs->tabTextColor(indexOf(tabs, CertificateCategory::Unset)), QColor(Qt::red));
    QVERIFY(!tabs->tabTextColor(indexOf(tabs, CertificateCategory::Class)).isValid());
    QVERIFY(!tabs->tabTextColor(indexOf(tabs, CertificateCategory::Equipment)).isValid());
}

void TestCertificateCategoryTabs::pickingATabShowsOnlyThatCategory()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);

    tabs->setCurrentIndex(indexOf(tabs, CertificateCategory::Class));
    QCOMPARE(tableOf(widget)->rowCount(), 1);

    tabs->setCurrentIndex(indexOf(tabs, CertificateCategory::Other));
    QCOMPARE(tableOf(widget)->rowCount(), 0);

    // Back to All, and everything is there again.
    tabs->setCurrentIndex(indexOf(tabs, CertificateCategory::Unset));
    QCOMPARE(tableOf(widget)->rowCount(), 4);
}

void TestCertificateCategoryTabs::changingTabClearsTheSelection()
{
    BUILD_WIDGET(widget);

    QTableWidget* table = tableOf(widget);
    table->selectRow(0);
    QVERIFY(!table->selectedItems().isEmpty());

    // §3: so Renew, Delete and the attachment actions can never act on a row
    // that has just been hidden.
    tabsOf(widget)->setCurrentIndex(indexOf(tabsOf(widget), CertificateCategory::Equipment));
    QVERIFY2(table->selectedItems().isEmpty(), "changing tab must leave nothing selected");
}

void TestCertificateCategoryTabs::theDrillDownComesBackToAll()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);

    // Somebody is looking at Equipment, where nothing needs attention, when
    // the alert banner says "show me what needs attention".
    tabs->setCurrentIndex(indexOf(tabs, CertificateCategory::Equipment));
    QCOMPARE(tableOf(widget)->rowCount(), 1);

    widget.setNeedsAttentionFilter(true);

    // §3: All as well as the checkbox, so nothing the banner is pointing at
    // is hidden behind another tab.
    QCOMPARE(tabs->currentIndex(), indexOf(tabs, CertificateCategory::Unset));
    QVERIFY(attentionCheckOf(widget)->isChecked());

    // And what is left is the expired statutory certificate, which is the
    // one the banner was about.
    QCOMPARE(tableOf(widget)->rowCount(), 1);
}

QTEST_MAIN(TestCertificateCategoryTabs)
#include "tst_CertificateCategoryTabs.moc"
