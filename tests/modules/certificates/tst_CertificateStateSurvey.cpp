#include "CertificateStateTestSupport.h"

#include <QtTest>

using namespace CertificateStateTestSupport;

// certificate-control-spec.md §4.8, the survey-matching half: items 6, 7, 8,
// 10 and 11, plus §4.2's worst-of-two rule and §4.4's days-left rule.
class TestCertificateStateSurvey : public QObject
{
    Q_OBJECT

private slots:
    // §4.8 item 10 — boundaries are inclusive.
    void windowBoundariesAreInclusive();
    void dueSoonUsesTheThreshold();

    // §4.8 item 6 — a late survey still counts.
    void aSurveyCompletedAfterItsWindowClosedStillSatisfiesIt();
    void reasonRecordsThatASurveyWasLate();
    void aTimelySurveyIsNotReportedAsLate();

    // Matching order.
    void eachAnniversaryClaimsTheEarliestEndorsementAvailableToIt();
    void anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing();

    // §4.8 item 7
    void annualOverdueWhileIntermediateStillInWindow();
    void intermediateOverdueWhileAnnualStillInWindow();

    // §4.8 item 8
    void bothWindowsOpenAtOnceReportsTheAnnual();

    // §4.8 item 11
    void replacesAnnualLetsAnIntermediateSatisfyTheSecondAnnual();
    void additionalModeKeepsTheTracksIndependent();

    // §4.4
    void daysLeftCountsToWindowOpeningThenToItsClose();
};

void TestCertificateStateSurvey::windowBoundariesAreInclusive()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);

    // The day the window opens: already in window, not merely due soon.
    const CertificateState onOpening = computeCertificateState(
        certificate, {}, AlertThresholds(), windowOpens(first));
    QCOMPARE(onOpening.survey, SurveySeverity::InWindow);

    // The day it closes: still in window, not yet overdue.
    const CertificateState onClosing = computeCertificateState(
        certificate, {}, AlertThresholds(), windowCloses(first));
    QCOMPARE(onClosing.survey, SurveySeverity::InWindow);

    // The day after: overdue.
    const CertificateState afterClosing = computeCertificateState(
        certificate, {}, AlertThresholds(), windowCloses(first).addDays(1));
    QCOMPARE(afterClosing.survey, SurveySeverity::Overdue);
    QVERIFY(!afterClosing.isValid);
}

void TestCertificateStateSurvey::dueSoonUsesTheThreshold()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       opens       = windowOpens(anniversary(certificate, 1));

    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), opens.addDays(-90)).survey,
             SurveySeverity::DueSoon);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), opens.addDays(-91)).survey,
             SurveySeverity::NotDue);
}

void TestCertificateStateSurvey::aSurveyCompletedAfterItsWindowClosedStillSatisfiesIt()
{
    // §4.5 as amended: read literally, the original pseudocode would leave a
    // missed anniversary Overdue forever. A late survey is still a survey.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);
    const QDate       lateSurvey  = windowCloses(first).addDays(45);

    // Before it was done: overdue.
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(),
                                     lateSurvey.addDays(-1))
                 .survey,
             SurveySeverity::Overdue);

    // Once recorded, the first anniversary is resolved and the certificate is
    // valid again.
    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(lateSurvey, SurveyType::Annual)}, AlertThresholds(),
        lateSurvey);

    QVERIFY2(state.isValid, "a late survey still resolves the anniversary");
    QVERIFY(state.survey != SurveySeverity::Overdue);
    // Attention has moved on to the second anniversary.
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 2));
}

void TestCertificateStateSurvey::reasonRecordsThatASurveyWasLate()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);
    const QDate       lateSurvey  = windowCloses(first).addDays(45);

    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(lateSurvey, SurveyType::Annual)}, AlertThresholds(),
        lateSurvey);

    // Recorded in the existing reason string, not a new field on the struct.
    QVERIFY2(state.reason.contains(QStringLiteral("late"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("reason was: %1").arg(state.reason)));
}

void TestCertificateStateSurvey::aTimelySurveyIsNotReportedAsLate()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);

    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(first, SurveyType::Annual)}, AlertThresholds(), first);

    QVERIFY(!state.reason.contains(QStringLiteral("late"), Qt::CaseInsensitive));
}

void TestCertificateStateSurvey::eachAnniversaryClaimsTheEarliestEndorsementAvailableToIt()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));

    // Two surveys done on time, for the first two anniversaries.
    const QList<Endorsement> done{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
        endorsementOn(anniversary(certificate, 2), SurveyType::Annual),
    };

    const CertificateState state = computeCertificateState(
        certificate, done, AlertThresholds(), anniversary(certificate, 2));

    // The third anniversary is what remains.
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 3));
    QVERIFY(state.isValid);
}

void TestCertificateStateSurvey::anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       tooEarly    = windowOpens(anniversary(certificate, 1)).addDays(-1);

    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(tooEarly, SurveyType::Annual)}, AlertThresholds(),
        windowCloses(anniversary(certificate, 1)).addDays(1));

    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 1));
}

void TestCertificateStateSurvey::annualOverdueWhileIntermediateStillInWindow()
{
    // §4.8 item 7. Intermediate window spans anniversary 2 - 3mo to
    // anniversary 3 + 3mo, so inside it the second annual has already closed.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresIntermediateSurvey = true;

    const QDate today = anniversary(certificate, 3); // inside the intermediate window
    const QList<Endorsement> onlyFirstAnnual{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
    };

    const CertificateState state =
        computeCertificateState(certificate, onlyFirstAnnual, AlertThresholds(), today);

    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QCOMPARE(state.nextSurveyType, SurveyType::Annual);
    QVERIFY(!state.isValid);
}

void TestCertificateStateSurvey::intermediateOverdueWhileAnnualStillInWindow()
{
    // The reverse: every annual done on time, but the intermediate window has
    // closed with nothing recorded.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresIntermediateSurvey = true;

    const QDate intermediateCloses = windowCloses(anniversary(certificate, 3));
    const QDate today              = windowOpens(anniversary(certificate, 4)).addDays(5);
    QVERIFY(today > intermediateCloses); // the intermediate window has closed

    const QList<Endorsement> annualsDone{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
        endorsementOn(anniversary(certificate, 2), SurveyType::Annual),
        endorsementOn(anniversary(certificate, 3), SurveyType::Annual),
    };

    const CertificateState state =
        computeCertificateState(certificate, annualsDone, AlertThresholds(), today);

    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QCOMPARE(state.nextSurveyType, SurveyType::Intermediate);
    QVERIFY(!state.isValid);
}

void TestCertificateStateSurvey::bothWindowsOpenAtOnceReportsTheAnnual()
{
    // §4.8 item 8, and §3.4's priority rule: the annual closes sooner.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresIntermediateSurvey = true;

    // The second anniversary: its annual window and the intermediate window
    // both open three months before it.
    const QDate today = anniversary(certificate, 2);

    const QList<Endorsement> firstDone{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
    };

    const CertificateState state =
        computeCertificateState(certificate, firstDone, AlertThresholds(), today);

    QCOMPARE(state.survey, SurveySeverity::InWindow);
    QCOMPARE(state.nextSurveyType, SurveyType::Annual);
    QCOMPARE(state.display, DisplayStatus::SurveyDue);
}

void TestCertificateStateSurvey::replacesAnnualLetsAnIntermediateSatisfyTheSecondAnnual()
{
    // §4.8 item 11.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresIntermediateSurvey = true;
    certificate.intermediateMode           = IntermediateMode::ReplacesAnnual;

    const QDate second = anniversary(certificate, 2);
    const QList<Endorsement> recorded{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
        endorsementOn(second, SurveyType::Intermediate), // stands in for the 2nd annual
    };

    const CertificateState state =
        computeCertificateState(certificate, recorded, AlertThresholds(),
                                windowCloses(second).addDays(1));

    QVERIFY2(state.isValid, "the intermediate survey replaced the second annual");
    QVERIFY(state.survey != SurveySeverity::Overdue);
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 3));
}

void TestCertificateStateSurvey::additionalModeKeepsTheTracksIndependent()
{
    // The same data under the default mode leaves the second annual unmet.
    Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    certificate.requiresIntermediateSurvey = true;
    certificate.intermediateMode           = IntermediateMode::Additional;

    const QDate second = anniversary(certificate, 2);
    const QList<Endorsement> recorded{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
        endorsementOn(second, SurveyType::Intermediate),
    };

    const CertificateState state =
        computeCertificateState(certificate, recorded, AlertThresholds(),
                                windowCloses(second).addDays(1));

    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QCOMPARE(state.nextSurveyType, SurveyType::Annual);
    QCOMPARE(state.nextAnniversary, second);
}

void TestCertificateStateSurvey::daysLeftCountsToWindowOpeningThenToItsClose()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);

    // Before the window opens: days until it opens (plan ahead).
    const QDate before = windowOpens(first).addDays(-10);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), before).daysLeft, 10);

    // Once open: days until it closes (how urgent).
    const QDate inside = windowOpens(first).addDays(10);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), inside).daysLeft,
             inside.daysTo(windowCloses(first)));

    // Overdue: negative, i.e. how long ago it closed.
    const QDate after = windowCloses(first).addDays(7);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), after).daysLeft, -7);
}

QTEST_APPLESS_MAIN(TestCertificateStateSurvey)
#include "tst_CertificateStateSurvey.moc"
