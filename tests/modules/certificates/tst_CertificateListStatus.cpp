#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateListWidget.h"
#include "modules/certificates/ui/StatusItem.h"

#include <QCheckBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-list-status-spec §9, the nine edge cases.
//
// The widget reads QDate::currentDate() itself (§8), so every certificate here
// is seeded with dates relative to today. That keeps each expected status
// fixed no matter which day the suite runs on.
class TestCertificateListStatus : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void neverExpiringCertificateShowsValidWithBlankSurveyAndDaysLeft();   // 1
    void upcomingSurveyWindowIsShownLongBeforeItOpens();                   // 2
    void missedAnnualSurveyShowsSurveyOverdueWithNegativeDaysLeft();       // 3
    void expiredCertificateShowsExpiredWithNegativeDaysLeft();             // 4
    void overdueSurveyOutranksAnImminentExpiry();                          // 5
    void everyTrackSatisfiedLeavesSurveyColumnsBlank();                    // 6
    void filterOnAnAllValidVesselShowsAnEmptyTableNotThePrompt();          // 7
    void statusColumnSortsBySeverityNotAlphabetically();                   // 8
    void defaultSortIsStillListNumberAscending();                          // 9

    // Supporting the above.
    void filterKeepsRowsThatNeedAttention();
    void validRowsCarryNoBackgroundHighlight();
    void urgentRowsCarryABackgroundHighlight();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static QTableWidget* tableOf(CertificateListWidget& widget)
    {
        return widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    }

    static QCheckBox* filterOf(CertificateListWidget& widget)
    {
        return widget.findChild<QCheckBox*>(QStringLiteral("needsAttentionCheck"));
    }

    static bool showingPrompt(CertificateListWidget& widget)
    {
        auto* stack = widget.findChild<QStackedWidget*>(QStringLiteral("certificateStack"));
        return stack != nullptr && stack->currentIndex() == 0;
    }

    // Column indices, matching certificate-list-status-spec §3.
    enum Col { No = 0, Name = 1, Status = 2, Expiry = 3, From = 4, To = 5, Days = 6 };

    static QString cell(CertificateListWidget& widget, int row, int column)
    {
        QTableWidgetItem* item = tableOf(widget)->item(row, column);
        return item == nullptr ? QString() : item->text();
    }

    static QStringList columnContents(CertificateListWidget& widget, int column)
    {
        QStringList   values;
        QTableWidget* table = tableOf(widget);
        for (int row = 0; row < table->rowCount(); ++row) {
            values.append(cell(widget, row, column));
        }
        return values;
    }

    QString m_connectionName;
    QString m_vesselId = QStringLiteral("v1");
};

void TestCertificateListStatus::init()
{
    m_connectionName = QStringLiteral("liststatus_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateListStatus::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateListStatus::neverExpiringCertificateShowsValidWithBlankSurveyAndDaysLeft()
{
    // §9 case 1. A Tonnage Certificate, typically.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Tonnage"),
                                QStringLiteral("1"), today.addYears(-1), QDate(),
                                /*requiresAnnual=*/false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QCOMPARE(tableOf(widget)->rowCount(), 1);
    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::Valid));
    QCOMPARE(cell(widget, 0, Expiry), QStringLiteral("Does not expire"));
    QVERIFY2(cell(widget, 0, From).isEmpty(), "no survey required, so no window to show");
    QVERIFY2(cell(widget, 0, To).isEmpty(), "no survey required, so no window to show");
    // Blank, not "0" — a zero here would read as "due today".
    QVERIFY2(cell(widget, 0, Days).isEmpty(), "a never-expiring certificate has no days left");
}

void TestCertificateListStatus::upcomingSurveyWindowIsShownLongBeforeItOpens()
{
    // §9 case 2. Issued today with five years to run: the first anniversary is
    // a year away and its window opens in about nine months — far outside the
    // 90-day "due soon" threshold, so the status is still Valid. The window
    // dates must be shown anyway.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"), today,
                                today.addYears(5), /*requiresAnnual=*/true);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::Valid));
    QVERIFY2(!cell(widget, 0, From).isEmpty(), "the window is shown regardless of urgency");
    QVERIFY2(!cell(widget, 0, To).isEmpty(), "the window is shown regardless of urgency");

    // The first anniversary is one year out; its window opens three months
    // before that.
    const QDate expectedOpens = today.addYears(1).addMonths(-3);
    QCOMPARE(cell(widget, 0, From), expectedOpens.toString(QStringLiteral("dd MMM yyyy")));
    QCOMPARE(cell(widget, 0, Days).toInt(), today.daysTo(expectedOpens));
}

void TestCertificateListStatus::missedAnnualSurveyShowsSurveyOverdueWithNegativeDaysLeft()
{
    // §9 case 3. Expiry is still a year away, but the first anniversary's
    // window closed long ago with nothing recorded against it.
    QSqlDatabase db     = database();
    const QDate  today  = QDate::currentDate();
    const QDate  expiry = today.addYears(1);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                expiry.addYears(-5), expiry, /*requiresAnnual=*/true);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::SurveyOverdue));
    QVERIFY2(cell(widget, 0, Days).toInt() < 0, "days left counts back from a closed window");
    QVERIFY2(!cell(widget, 0, From).isEmpty(), "the missed window is still worth showing");
}

void TestCertificateListStatus::expiredCertificateShowsExpiredWithNegativeDaysLeft()
{
    // §9 case 4.
    QSqlDatabase db     = database();
    const QDate  today  = QDate::currentDate();
    const QDate  expiry = today.addDays(-10);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), expiry.addYears(-5), expiry,
                                /*requiresAnnual=*/false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::Expired));
    QCOMPARE(cell(widget, 0, Days).toInt(), -10);
}

void TestCertificateListStatus::overdueSurveyOutranksAnImminentExpiry()
{
    // §9 case 5. Both wrong at once. The screen adds no logic of its own — it
    // shows whichever DisplayStatus comes back, and step 6 already settled
    // that an overdue survey outranks a merely critical expiry.
    QSqlDatabase db     = database();
    const QDate  today  = QDate::currentDate();
    const QDate  expiry = today.addDays(10); // critical on its own
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Equipment"), QStringLiteral("1"),
                                expiry.addYears(-5), expiry, /*requiresAnnual=*/true);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::SurveyOverdue));
}

void TestCertificateListStatus::everyTrackSatisfiedLeavesSurveyColumnsBlank()
{
    // §9 case 6. A two-year certificate with its single anniversary already
    // endorsed: nothing outstanding but the renewal, so there is no window.
    QSqlDatabase db     = database();
    const QDate  today  = QDate::currentDate();
    const QDate  expiry = today.addYears(1);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), expiry,
                                /*requiresAnnual=*/true);
    // The only anniversary falls today; its window opened three months ago.
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addMonths(-1));

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QVERIFY2(cell(widget, 0, From).isEmpty(), "only the renewal remains, so there is no window");
    QVERIFY2(cell(widget, 0, To).isEmpty(), "only the renewal remains, so there is no window");
    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::Valid));
    // Days left falls back to the expiry, per §4.4.
    QCOMPARE(cell(widget, 0, Days).toInt(), today.daysTo(expiry));
}

void TestCertificateListStatus::filterOnAnAllValidVesselShowsAnEmptyTableNotThePrompt()
{
    // §9 case 7. An empty table means "all fine right now"; the prompt means
    // "no vessel chosen". They must not be confused.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Tonnage"),
                                QStringLiteral("1"), today.addYears(-1), QDate(), false);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("2"), today, today.addYears(5), false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);
    QCOMPARE(tableOf(widget)->rowCount(), 2);

    filterOf(widget)->setChecked(true);

    QCOMPARE(tableOf(widget)->rowCount(), 0);
    QVERIFY2(!showingPrompt(widget), "a filtered-to-nothing table is not the select-a-vessel state");
}

void TestCertificateListStatus::filterKeepsRowsThatNeedAttention()
{
    QSqlDatabase db     = database();
    const QDate  today  = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Fine"),
                                QStringLiteral("1"), today, today.addYears(5), false);
    const QDate expired = today.addDays(-5);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Lapsed"),
                                QStringLiteral("2"), expired.addYears(-5), expired, false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);
    QCOMPARE(tableOf(widget)->rowCount(), 2);

    filterOf(widget)->setChecked(true);
    QCOMPARE(tableOf(widget)->rowCount(), 1);
    QCOMPARE(cell(widget, 0, Name), QStringLiteral("Lapsed"));

    // Unchecking brings the healthy one back.
    filterOf(widget)->setChecked(false);
    QCOMPARE(tableOf(widget)->rowCount(), 2);
}

void TestCertificateListStatus::statusColumnSortsBySeverityNotAlphabetically()
{
    // §9 case 8. Alphabetically "Expired" precedes "Valid", which says nothing
    // about urgency; the column has to sort by severity instead.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();

    // Valid.
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Healthy"),
                                QStringLiteral("1"), today, today.addYears(5), false);
    // Expired.
    const QDate expired = today.addDays(-5);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Lapsed"),
                                QStringLiteral("2"), expired.addYears(-5), expired, false);
    // Expiring soon (45 days out, inside the 60-day threshold).
    const QDate soon = today.addDays(45);
    seedCertificateWithSchedule(db, QStringLiteral("c3"), m_vesselId, QStringLiteral("Closing"),
                                QStringLiteral("3"), soon.addYears(-5), soon, false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QTableWidget* table = tableOf(widget);

    // What a click on the header does.
    table->sortByColumn(Status, Qt::AscendingOrder);
    QCOMPARE(columnContents(widget, Status),
             QStringList({StatusItem::labelFor(DisplayStatus::Valid),
                          StatusItem::labelFor(DisplayStatus::ExpiringSoon),
                          StatusItem::labelFor(DisplayStatus::Expired)}));

    // Clicking again surfaces the most urgent first.
    table->sortByColumn(Status, Qt::DescendingOrder);
    QCOMPARE(columnContents(widget, Status),
             QStringList({StatusItem::labelFor(DisplayStatus::Expired),
                          StatusItem::labelFor(DisplayStatus::ExpiringSoon),
                          StatusItem::labelFor(DisplayStatus::Valid)}));
}

void TestCertificateListStatus::defaultSortIsStillListNumberAscending()
{
    // §9 case 9. Unchanged from step 5, despite the new columns.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Alpha"),
                                QStringLiteral("15D"), today, today.addYears(5), false);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Bravo"),
                                QStringLiteral("3"), today, today.addYears(5), false);
    seedCertificateWithSchedule(db, QStringLiteral("c3"), m_vesselId, QStringLiteral("Charlie"),
                                QStringLiteral("9"), today, today.addYears(5), false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    // No header clicked: still the company numbering order, not alphabetical
    // by name and not by status.
    QCOMPARE(columnContents(widget, No),
             QStringList({QStringLiteral("3"), QStringLiteral("9"), QStringLiteral("15D")}));
}

void TestCertificateListStatus::validRowsCarryNoBackgroundHighlight()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Healthy"),
                                QStringLiteral("1"), today, today.addYears(5), false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    // "Valid means no highlight" (§4): the cell keeps the default row
    // background rather than being painted a reassuring colour.
    QTableWidgetItem* status = tableOf(widget)->item(0, Status);
    QCOMPARE(status->background().style(), Qt::NoBrush);
}

void TestCertificateListStatus::urgentRowsCarryABackgroundHighlight()
{
    QSqlDatabase db      = database();
    const QDate  today   = QDate::currentDate();
    const QDate  expired = today.addDays(-5);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Lapsed"),
                                QStringLiteral("1"), expired.addYears(-5), expired, false);

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateListWidget widget(certificates, endorsements, context);
    widget.setVesselId(m_vesselId);

    QTableWidgetItem* status = tableOf(widget)->item(0, Status);
    QVERIFY(status->background().style() != Qt::NoBrush);
    QCOMPARE(status->background().color(), QColor(QStringLiteral("#F8D7DA")));
    QCOMPARE(status->foreground().color(), QColor(QStringLiteral("#842029")));
}

QTEST_MAIN(TestCertificateListStatus)
#include "tst_CertificateListStatus.moc"
