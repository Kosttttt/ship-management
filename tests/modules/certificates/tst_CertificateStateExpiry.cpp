#include "CertificateStateTestSupport.h"

#include <QtTest>

using namespace CertificateStateTestSupport;

// certificate-control-spec.md §4.8, the expiry-driven half: items 1, 2, 3, 4,
// 9, 10 (expiry boundaries), 13 and 14. Pure function, no database, no
// widgets (CLAUDE.md §4 rule 1).
class TestCertificateStateExpiry : public QObject
{
    Q_OBJECT

private slots:
    // §4.8 item 13
    void noExpiryIsAlwaysValidAndNeedsNoSurvey();
    void noExpiryIgnoresSurveyFlagsRatherThanCrashing();

    // §4.3 expiry tiers
    void expiryTiers_data();
    void expiryTiers();
    void thresholdsAreReadFromTheArgumentNotHardcoded();

    // §4.8 item 4
    void freshCertificateWithNoEndorsementsIsValid();

    // §4.8 item 1
    void leapDayExpiryProducesAnniversariesInNonLeapYears();
    // §4.8 item 2
    void windowBoundaryClampsIntoAThirtyDayMonth();
    // §4.8 item 3
    void shortTermCertificateHasNoAnniversaries();
    void twoYearCertificateHasOneAnniversary();

    // §4.8 item 9
    void expiringSoonAndSurveyOverdueAtOnce();

    // §4.8 item 14
    void initialAndRenewalEndorsementsSatisfyNothing();

    // §4.4
    void daysLeftCountsToExpiryWhenNoSurveyIsOutstanding();
};

void TestCertificateStateExpiry::noExpiryIsAlwaysValidAndNeedsNoSurvey()
{
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.expiryDate  = QDate(); // never expires
    certificate.requiresAnnualSurvey = false;

    const CertificateState state =
        computeCertificateState(certificate, {}, AlertThresholds(), QDate(2026, 8, 13));

    QCOMPARE(state.expiry, ExpirySeverity::Valid);
    QCOMPARE(state.survey, SurveySeverity::NotRequired);
    QCOMPARE(state.display, DisplayStatus::Valid);
    QVERIFY(state.isValid);
    QVERIFY(!state.nextAnniversary.isValid());
}

void TestCertificateStateExpiry::noExpiryIgnoresSurveyFlagsRatherThanCrashing()
{
    // The repository rejects this combination before it can be stored; the
    // function still has to answer sensibly if handed one, since §4 says it
    // returns immediately without touching the anniversary calculation.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.expiryDate                 = QDate();
    certificate.requiresAnnualSurvey       = true;
    certificate.requiresIntermediateSurvey = true;

    const CertificateState state =
        computeCertificateState(certificate, {}, AlertThresholds(), QDate(2026, 8, 13));

    QCOMPARE(state.expiry, ExpirySeverity::Valid);
    QCOMPARE(state.survey, SurveySeverity::NotRequired);
    QVERIFY(state.isValid);
}

void TestCertificateStateExpiry::expiryTiers_data()
{
    QTest::addColumn<QDate>("today");
    QTest::addColumn<ExpirySeverity>("expected");

    const QDate expiry(2031, 2, 4);

    QTest::newRow("day after expiry")  << expiry.addDays(1) << ExpirySeverity::Expired;
    QTest::newRow("on expiry day")     << expiry            << ExpirySeverity::Critical;
    QTest::newRow("30 days before")    << expiry.addDays(-30) << ExpirySeverity::Critical;
    QTest::newRow("31 days before")    << expiry.addDays(-31) << ExpirySeverity::ExpiringSoon;
    QTest::newRow("60 days before")    << expiry.addDays(-60) << ExpirySeverity::ExpiringSoon;
    QTest::newRow("61 days before")    << expiry.addDays(-61) << ExpirySeverity::Valid;
}

void TestCertificateStateExpiry::expiryTiers()
{
    QFETCH(QDate, today);
    QFETCH(ExpirySeverity, expected);

    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresAnnualSurvey = false; // isolate the expiry rule

    const CertificateState state =
        computeCertificateState(certificate, {}, AlertThresholds(), today);

    QCOMPARE(state.expiry, expected);
}

void TestCertificateStateExpiry::thresholdsAreReadFromTheArgumentNotHardcoded()
{
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresAnnualSurvey = false;

    AlertThresholds wider;
    wider.criticalDays     = 100;
    wider.expiringSoonDays = 200;

    const QDate today = certificate.expiryDate.addDays(-90);

    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), today).expiry,
             ExpirySeverity::Valid);
    QCOMPARE(computeCertificateState(certificate, {}, wider, today).expiry,
             ExpirySeverity::Critical);
}

void TestCertificateStateExpiry::freshCertificateWithNoEndorsementsIsValid()
{
    // Issued today, five years to run: nothing is due yet.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       today       = certificate.issueDate;

    const CertificateState state =
        computeCertificateState(certificate, {}, AlertThresholds(), today);

    QCOMPARE(state.expiry, ExpirySeverity::Valid);
    QCOMPARE(state.survey, SurveySeverity::NotDue);
    QCOMPARE(state.display, DisplayStatus::Valid);
    QVERIFY(state.isValid);
    // The first anniversary is what it is counting toward.
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 1));
}

void TestCertificateStateExpiry::leapDayExpiryProducesAnniversariesInNonLeapYears()
{
    // Expiry 29 Feb 2032 (a leap year); 2031, 2030, 2029 are not leap years,
    // so those anniversaries must land on 28 February.
    const Certificate certificate = certificateExpiring(QDate(2032, 2, 29));
    QCOMPARE(certificate.issueDate, QDate(2027, 2, 28)); // Qt clamps here too

    const CertificateState state = computeCertificateState(
        certificate, {}, AlertThresholds(), QDate(2028, 1, 1));

    QCOMPARE(state.nextAnniversary, QDate(2028, 2, 29)); // 2028 is a leap year

    const CertificateState later = computeCertificateState(
        certificate, {endorsementOn(QDate(2028, 2, 29), SurveyType::Annual)},
        AlertThresholds(), QDate(2029, 1, 1));

    QCOMPARE(later.nextAnniversary, QDate(2029, 2, 28)); // clamped
}

void TestCertificateStateExpiry::windowBoundaryClampsIntoAThirtyDayMonth()
{
    // Expiry on the 31st: an anniversary on 31 January opens three months
    // earlier (31 October) and closes three months later, which would be
    // 31 April — a date that does not exist, so it clamps to 30 April.
    const Certificate certificate = certificateExpiring(QDate(2031, 1, 31));
    const QDate       first       = anniversary(certificate, 1);
    QCOMPARE(first, QDate(2027, 1, 31));

    const CertificateState state = computeCertificateState(
        certificate, {}, AlertThresholds(), QDate(2027, 2, 1));

    QCOMPARE(state.windowOpens, QDate(2026, 10, 31));
    QCOMPARE(state.windowCloses, QDate(2027, 4, 30));
}

void TestCertificateStateExpiry::shortTermCertificateHasNoAnniversaries()
{
    // A one-year interim certificate: nothing between issue and expiry.
    const Certificate certificate = certificateExpiring(QDate(2027, 2, 4), /*termYears=*/1);

    const CertificateState state = computeCertificateState(
        certificate, {}, AlertThresholds(), QDate(2026, 8, 13));

    QCOMPARE(state.survey, SurveySeverity::NotDue);
    QVERIFY(state.isValid);
    // Nothing outstanding, so the next action is the renewal.
    QCOMPARE(state.nextSurveyType, SurveyType::Renewal);
}

void TestCertificateStateExpiry::twoYearCertificateHasOneAnniversary()
{
    const Certificate certificate = certificateExpiring(QDate(2028, 2, 4), /*termYears=*/2);

    const CertificateState state = computeCertificateState(
        certificate, {}, AlertThresholds(), QDate(2026, 3, 1));

    QCOMPARE(state.nextAnniversary, QDate(2027, 2, 4));
    QCOMPARE(state.nextSurveyType, SurveyType::Annual);
}

void TestCertificateStateExpiry::expiringSoonAndSurveyOverdueAtOnce()
{
    // §4.8 item 9. The last annual was never done and expiry is close: both
    // are wrong at once, and the survey being overdue is the more severe.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       today       = QDate(2031, 1, 15); // 20 days to expiry

    const CertificateState state =
        computeCertificateState(certificate, {}, AlertThresholds(), today);

    QCOMPARE(state.expiry, ExpirySeverity::Critical);
    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QCOMPARE(state.display, DisplayStatus::SurveyOverdue);
    QVERIFY(!state.isValid);
}

void TestCertificateStateExpiry::initialAndRenewalEndorsementsSatisfyNothing()
{
    // §4.8 item 14: stored and returned, but never matched against a window.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);
    const QDate       today       = windowCloses(first).addDays(1);

    const QList<Endorsement> unhelpful{
        endorsementOn(certificate.issueDate, SurveyType::Initial),
        endorsementOn(first, SurveyType::Renewal),
    };

    const CertificateState state =
        computeCertificateState(certificate, unhelpful, AlertThresholds(), today);

    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QVERIFY(!state.isValid);

    // The same dates, recorded as an annual survey, do satisfy it.
    const CertificateState satisfied = computeCertificateState(
        certificate, {endorsementOn(first, SurveyType::Annual)}, AlertThresholds(), today);
    QVERIFY(satisfied.isValid);
}

void TestCertificateStateExpiry::daysLeftCountsToExpiryWhenNoSurveyIsOutstanding()
{
    // §4.4: no survey outstanding, so the actionable number is the expiry.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresAnnualSurvey = false;

    const QDate today = QDate(2031, 1, 5);

    const CertificateState state =
        computeCertificateState(certificate, {}, AlertThresholds(), today);

    QCOMPARE(state.daysLeft, today.daysTo(certificate.expiryDate));
    QCOMPARE(state.nextSurveyType, SurveyType::Renewal);
}

QTEST_APPLESS_MAIN(TestCertificateStateExpiry)
#include "tst_CertificateStateExpiry.moc"
