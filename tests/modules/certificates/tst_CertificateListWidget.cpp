#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QStackedWidget>
#include <QTableWidget>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-crud-spec §7 item 9: the prompt state, and rescoping when the
// toolbar selector changes.
class TestCertificateListWidget : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void officeWithNoVesselShowsThePromptNotAnEmptyTable();
    void choosingAVesselShowsThatVesselsCertificates();
    void switchingVesselReloadsForTheNewVessel();
    void clearingTheSelectionReturnsToThePrompt();
    void aVesselWithNoCertificatesShowsAnEmptyTableNotThePrompt();
    void vesselModeOpensStraightToItsOwnList();

    void defaultSortIsByListNumberNotByName();
    void certificatesWithoutAListNumberSortLast();
    void columnsAreNumberNameStatusExpirySurveyDaysLeft();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // Page 0 is the prompt, page 1 is the list.
    static bool showingPrompt(CertificateListWidget& widget)
    {
        auto* stack = widget.findChild<QStackedWidget*>(QStringLiteral("certificateStack"));
        return stack != nullptr && stack->currentIndex() == 0;
    }

    static int rowsShown(CertificateListWidget& widget)
    {
        auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
        return table == nullptr ? -1 : table->rowCount();
    }

    static QTableWidget* tableOf(CertificateListWidget& widget)
    {
        return widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    }

    // The visible contents of one column, top row first.
    static QStringList columnContents(CertificateListWidget& widget, int column)
    {
        QStringList values;
        QTableWidget* table = tableOf(widget);
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem* item = table->item(row, column);
            values.append(item == nullptr ? QString() : item->text());
        }
        return values;
    }

    QString m_connectionName;
};

void TestCertificateListWidget::init()
{
    m_connectionName = QStringLiteral("certlist_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v1"), QStringLiteral("IOPP"));
    seedCertificate(db, QStringLiteral("c3"), QStringLiteral("v2"), QStringLiteral("Tonnage"));
}

void TestCertificateListWidget::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateListWidget::officeWithNoVesselShowsThePromptNotAnEmptyTable()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);

    QVERIFY2(showingPrompt(widget),
             "with no vessel chosen the screen must say so, not show an empty table");
    QVERIFY(widget.vesselId().isEmpty());
}

void TestCertificateListWidget::choosingAVesselShowsThatVesselsCertificates()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);
    widget.setVesselId(QStringLiteral("v1"));

    QVERIFY(!showingPrompt(widget));
    QCOMPARE(rowsShown(widget), 2);
}

void TestCertificateListWidget::switchingVesselReloadsForTheNewVessel()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);

    widget.setVesselId(QStringLiteral("v1"));
    QCOMPARE(rowsShown(widget), 2);

    // What MainWindow does when the toolbar selector changes.
    widget.setVesselId(QStringLiteral("v2"));
    QCOMPARE(rowsShown(widget), 1);
    QCOMPARE(widget.vesselId(), QStringLiteral("v2"));
}

void TestCertificateListWidget::clearingTheSelectionReturnsToThePrompt()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);
    widget.setVesselId(QStringLiteral("v1"));
    QVERIFY(!showingPrompt(widget));

    widget.setVesselId(QString());
    QVERIFY(showingPrompt(widget));
    QCOMPARE(rowsShown(widget), 0);
}

void TestCertificateListWidget::aVesselWithNoCertificatesShowsAnEmptyTableNotThePrompt()
{
    // An actual vessel with zero certificates is a normal state, not an error
    // and not the "choose a vessel" prompt.
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);
    widget.setVesselId(QStringLiteral("v-with-nothing"));

    QVERIFY(!showingPrompt(widget));
    QCOMPARE(rowsShown(widget), 0);
}

void TestCertificateListWidget::vesselModeOpensStraightToItsOwnList()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);

    // No selector exists in VESSEL mode, so the widget must already be scoped.
    QVERIFY2(!showingPrompt(widget), "a vessel installation never sees the prompt");
    QCOMPARE(widget.vesselId(), QStringLiteral("v1"));
    QCOMPARE(rowsShown(widget), 2);
}

// certificate-crud-spec §8.5 items 14 and 15, as the list actually shows them.
void TestCertificateListWidget::defaultSortIsByListNumberNotByName()
{
    QSqlDatabase db = database();
    // Names deliberately in the opposite order to the numbers, so a list still
    // sorted by name would be obvious.
    seedCertificate(db, QStringLiteral("s1"), QStringLiteral("vs"), QStringLiteral("Alpha"),
                    false, QStringLiteral("15D"));
    seedCertificate(db, QStringLiteral("s2"), QStringLiteral("vs"), QStringLiteral("Bravo"),
                    false, QStringLiteral("3"));
    seedCertificate(db, QStringLiteral("s3"), QStringLiteral("vs"), QStringLiteral("Charlie"),
                    false, QStringLiteral("9"));
    seedCertificate(db, QStringLiteral("s4"), QStringLiteral("vs"), QStringLiteral("Delta"),
                    false, QStringLiteral("3B"));
    seedCertificate(db, QStringLiteral("s5"), QStringLiteral("vs"), QStringLiteral("Echo"),
                    false, QStringLiteral("3A"));

    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);
    widget.setVesselId(QStringLiteral("vs"));

    const QStringList expected{QStringLiteral("3"), QStringLiteral("3A"), QStringLiteral("3B"),
                               QStringLiteral("9"), QStringLiteral("15D")};
    QCOMPARE(columnContents(widget, 0), expected);
}

void TestCertificateListWidget::certificatesWithoutAListNumberSortLast()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("s1"), QStringLiteral("vs"), QStringLiteral("Unnumbered"),
                    false, QString());
    seedCertificate(db, QStringLiteral("s2"), QStringLiteral("vs"), QStringLiteral("Numbered"),
                    false, QStringLiteral("15D"));
    seedCertificate(db, QStringLiteral("s3"), QStringLiteral("vs"), QStringLiteral("Also none"),
                    false, QString());
    seedCertificate(db, QStringLiteral("s4"), QStringLiteral("vs"), QStringLiteral("Low"),
                    false, QStringLiteral("3"));

    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);
    widget.setVesselId(QStringLiteral("vs"));

    const QStringList numbers = columnContents(widget, 0);
    QCOMPARE(numbers.size(), 4);
    // The ones the fleet refers to by number stay together at the top.
    QCOMPARE(numbers.at(0), QStringLiteral("3"));
    QCOMPARE(numbers.at(1), QStringLiteral("15D"));
    QVERIFY(numbers.at(2).isEmpty());
    QVERIFY(numbers.at(3).isEmpty());
}

void TestCertificateListWidget::columnsAreNumberNameStatusExpirySurveyDaysLeft()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("s1"), QStringLiteral("vs"), QStringLiteral("Load Line"),
                    false, QStringLiteral("7"));

    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    EndorsementRepository endorsements(db, context);
    CertificateListWidget widget(repository, endorsements, context);
    widget.setVesselId(QStringLiteral("vs"));

    // certificate-list-status-spec §3. Issue Date and Category left this
    // screen in step 7; they are still on the edit dialog.
    QTableWidget* table = tableOf(widget);
    QCOMPARE(table->columnCount(), 7);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("No."));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Name"));
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QStringLiteral("Status"));
    QCOMPARE(table->horizontalHeaderItem(3)->text(), QStringLiteral("Expiry Date"));
    QCOMPARE(table->horizontalHeaderItem(4)->text(), QStringLiteral("Survey From"));
    QCOMPARE(table->horizontalHeaderItem(5)->text(), QStringLiteral("Survey To"));
    QCOMPARE(table->horizontalHeaderItem(6)->text(), QStringLiteral("Days Left"));

    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("04 Feb 2031"));
}

QTEST_MAIN(TestCertificateListWidget)
#include "tst_CertificateListWidget.moc"
