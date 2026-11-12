#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateListRows.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QCheckBox>
#include <QPainter>
#include <QPixmap>
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
    void aTabWithSomethingNeedingAttentionIsMarked();
    void theAttentionMarkIsActuallyPainted();

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

    // Whether anything clearly red was *painted* inside one tab.
    //
    // §6 exists because the first build only ever checked what was stored.
    // Antialiasing blends the edges of a small shape, so this looks for a
    // pixel that is plainly more red than anything else rather than for an
    // exact Qt::red.
    //
    // The rect comes from tabRect() in logical pixels while grab() returns a
    // pixmap in device pixels, so it is scaled by the device pixel ratio
    // before being read — without that it would look in the wrong place on
    // any screen above 100%.
    static bool hasRedPixelIn(const QPixmap& grabbed, const QRect& logicalArea)
    {
        const QImage image = grabbed.toImage();
        const qreal  ratio = grabbed.devicePixelRatio();

        const QRect area = QRectF(logicalArea.x() * ratio, logicalArea.y() * ratio,
                                  logicalArea.width() * ratio, logicalArea.height() * ratio)
                               .toAlignedRect()
                               .intersected(image.rect());

        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.red() > 150 && pixel.green() < 100 && pixel.blue() < 100) {
                    return true;
                }
            }
        }
        return false;
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

void TestCertificateCategoryTabs::aTabWithSomethingNeedingAttentionIsMarked()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);

    // §2 and §6: the expired Load Line is statutory, so that tab — and All —
    // carry the mark, and the categories holding only valid certificates do
    // not. The value of the mark is entirely in not being on everything.
    QVERIFY(!tabs->tabIcon(indexOf(tabs, CertificateCategory::Statutory)).isNull());
    QVERIFY(!tabs->tabIcon(indexOf(tabs, CertificateCategory::Unset)).isNull());
    QVERIFY(tabs->tabIcon(indexOf(tabs, CertificateCategory::Class)).isNull());
    QVERIFY(tabs->tabIcon(indexOf(tabs, CertificateCategory::Equipment)).isNull());
    QVERIFY(tabs->tabIcon(indexOf(tabs, CertificateCategory::Other)).isNull());

    // The tooltip stays, and is on exactly the marked tabs (§6).
    QVERIFY(!tabs->tabToolTip(indexOf(tabs, CertificateCategory::Statutory)).isEmpty());
    QVERIFY(tabs->tabToolTip(indexOf(tabs, CertificateCategory::Class)).isEmpty());
}

void TestCertificateCategoryTabs::theAttentionMarkIsActuallyPainted()
{
    BUILD_WIDGET(widget);

    QTabBar* tabs = tabsOf(widget);

    // No style is forced: this renders with whatever the program itself uses,
    // which on Windows 11 is the style that ignored setTabTextColor() and let
    // the first build ship a mark nobody could see (§6).
    //
    // Sized from its own hint rather than shown — grab() needs real geometry
    // and tabRect() needs the tabs laid out, and neither needs a window.
    tabs->resize(tabs->sizeHint());
    QVERIFY(tabs->width() > 0);

    const QPixmap painted = tabs->grab();
    QVERIFY(!painted.isNull());

    // Exactly the tabs that need attention, read off the pixels rather than
    // off a property: that is the whole point of this test existing.
    const QList<CertificateCategory> marked{CertificateCategory::Unset,
                                            CertificateCategory::Statutory};

    for (int index = 0; index < tabs->count(); ++index) {
        const auto category = static_cast<CertificateCategory>(tabs->tabData(index).toInt());
        const bool expected = marked.contains(category);

        QVERIFY2(hasRedPixelIn(painted, tabs->tabRect(index)) == expected,
                 qPrintable(QStringLiteral("tab '%1' %2 have shown something red")
                                .arg(tabs->tabText(index),
                                     expected ? QStringLiteral("should")
                                              : QStringLiteral("should not"))));
    }
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
