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

    // Matching order — the latest survey counts (survey-latest-counts-spec.md).
    void theLatestSurveyCoversEveryAnniversaryBeforeIt();
    void theRealLoadLineCaseWithOnlyTheLatestSurveyRecorded();
    void onlyTheLatestSurveyIsReportedLate();
    void aMissedEarlierAnnualIsNotReportedOnceALaterOneExists();
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
    void daysLeftAlwaysCountsToTheWindowClose();
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

void TestCertificateStateSurvey::theLatestSurveyCoversEveryAnniversaryBeforeIt()
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

void TestCertificateStateSurvey::theRealLoadLineCaseWithOnlyTheLatestSurveyRecorded()
{
    // survey-latest-counts-spec.md §1, with that certificate's real dates:
    // V TRE, No. 12, Load Line, issued 29 May 2022, expires 09 Mar 2027, one
    // annual endorsement recorded, 12 Apr 2026 at Iskenderun. The term is four
    // whole years, so the anniversaries are 09 Mar 2024, 2025 and 2026 — set
    // out literally here rather than through anniversary(), which assumes a
    // term of whole years from the issue date.
    Certificate certificate = certificateExpiring(QDate(2027, 3, 9));
    certificate.issueDate   = QDate(2022, 5, 29);
    certificate.name        = QStringLiteral("Load Line Certificate");

    const QDate third = QDate(2026, 3, 9); // its window: 09 Dec 2025 - 09 Jun 2026
    const QDate today = QDate(2026, 11, 10);

    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(QDate(2026, 4, 12), SurveyType::Annual)},
        AlertThresholds(), today);

    // The survey fell inside the third anniversary's window, so that one and
    // the two before it are all covered and nothing is outstanding. Under the
    // old claim-walk this row read "Overdue, -519 days" against a window that
    // had closed on 09 Jun 2025.
    QVERIFY2(state.survey != SurveySeverity::Overdue, qPrintable(state.reason));
    QCOMPARE(state.survey, SurveySeverity::NotDue);
    QCOMPARE(state.nextSurveyType, SurveyType::Renewal);
    QVERIFY(state.isValid);
    QVERIFY(!state.reason.contains(QStringLiteral("late"), Qt::CaseInsensitive));

    // Nothing outstanding, so the deadline is the expiry date, not a window
    // that closed over a year ago.
    QCOMPARE(state.dueDate, QDate(2027, 3, 9));
    QCOMPARE(state.daysLeft, today.daysTo(QDate(2027, 3, 9)));
    QVERIFY2(state.daysLeft != -519, "the old claim-walk's answer");

    // The other half of the rule: had the survey been a year earlier, the
    // anniversary after it would be what remains — the third, not the first.
    const CertificateState earlier = computeCertificateState(
        certificate, {endorsementOn(QDate(2025, 4, 12), SurveyType::Annual)},
        AlertThresholds(), today);

    QCOMPARE(earlier.nextAnniversary, third);
    QCOMPARE(earlier.nextSurveyType, SurveyType::Annual);
    QCOMPARE(earlier.survey, SurveySeverity::Overdue); // that window has closed
}

void TestCertificateStateSurvey::onlyTheLatestSurveyIsReportedLate()
{
    // survey-latest-counts-spec.md §3: the latest survey is the only one
    // judged for lateness. One survey, dated after its own window closed but
    // before the next window opens, covers that anniversary and every earlier
    // one — and the note names that anniversary, once.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       second      = anniversary(certificate, 2);
    const QDate       lateSurvey  = windowCloses(second).addDays(58);
    QVERIFY(lateSurvey > windowCloses(second));
    QVERIFY(lateSurvey < windowOpens(anniversary(certificate, 3)));

    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(lateSurvey, SurveyType::Annual)}, AlertThresholds(),
        lateSurvey);

    QVERIFY(state.survey != SurveySeverity::Overdue);
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 3));

    // Once, and for the second anniversary. The first is covered as well but
    // has no survey of its own to date a late claim from, so it is not named.
    QCOMPARE(state.reason.count(QStringLiteral("late"), Qt::CaseInsensitive), 1);
    QVERIFY2(state.reason.contains(second.toString(QStringLiteral("dd MMM yyyy"))),
             qPrintable(state.reason));
    QVERIFY(!state.reason.contains(
        anniversary(certificate, 1).toString(QStringLiteral("dd MMM yyyy"))));
}

void TestCertificateStateSurvey::aMissedEarlierAnnualIsNotReportedOnceALaterOneExists()
{
    // This records the developer's decision, deliberately — it is not a defect
    // waiting to be fixed. survey-latest-counts-spec.md §2: once a later
    // annual survey is recorded, a genuinely missed earlier one is no longer
    // flagged. The office tracks forward from the last survey, and a missed
    // annual is dealt with by the flag or class at the time. Changing this
    // test to demand the gap be reported would put back the bug in §1.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       second      = anniversary(certificate, 2); // never surveyed
    const QDate       third       = anniversary(certificate, 3);

    const QList<Endorsement> withAGap{
        endorsementOn(anniversary(certificate, 1), SurveyType::Annual),
        endorsementOn(third, SurveyType::Annual),
    };

    // Past the third window's close, so the old claim-walk reported the third
    // anniversary overdue here and called the third survey a late second one.
    const QDate today = windowCloses(third).addDays(28);

    const CertificateState state =
        computeCertificateState(certificate, withAGap, AlertThresholds(), today);

    QCOMPARE(state.survey, SurveySeverity::NotDue);
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 4));
    QVERIFY(state.isValid);

    // The missed second anniversary is not mentioned, and nothing is late.
    QVERIFY(!state.reason.contains(second.toString(QStringLiteral("dd MMM yyyy"))));
    QVERIFY(!state.reason.contains(QStringLiteral("late"), Qt::CaseInsensitive));
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

void TestCertificateStateSurvey::daysLeftAlwaysCountsToTheWindowClose()
{
    // §4.4, as corrected by certificate-survey-deadline-fix.md: one rule for
    // the whole life of an outstanding survey. The close is the deadline — the
    // last day the survey can still be done — so that is what the number
    // counts to, whether or not the window has opened yet. The opening is a
    // heads-up and drives the colour (SurveySeverity), not this number.
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       first       = anniversary(certificate, 1);
    const QDate       closes      = windowCloses(first);

    // Before the window opens: still the close, not the opening. This is the
    // case the fix changed, so it is asserted both ways round — the old
    // behaviour would have given 10 here.
    const QDate            before      = windowOpens(first).addDays(-10);
    const CertificateState beforeState =
        computeCertificateState(certificate, {}, AlertThresholds(), before);
    QCOMPARE(beforeState.survey, SurveySeverity::DueSoon);
    QCOMPARE(beforeState.daysLeft, before.daysTo(closes));
    QVERIFY2(beforeState.daysLeft != 10, "days left must not count to the window opening");

    // Once open: unchanged, days until it closes.
    const QDate inside = windowOpens(first).addDays(10);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), inside).daysLeft,
             inside.daysTo(closes));

    // Overdue: negative, i.e. how long ago it closed. Also unchanged.
    const QDate after = closes.addDays(7);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), after).daysLeft, -7);

    // Still true the far side of the opening, where the old rule would have
    // produced a NotDue row counting to a date a year before anything is due.
    const QDate farOut = windowOpens(first).addDays(-200);
    const CertificateState farState =
        computeCertificateState(certificate, {}, AlertThresholds(), farOut);
    QCOMPARE(farState.survey, SurveySeverity::NotDue);
    QCOMPARE(farState.daysLeft, farOut.daysTo(closes));

    // dueDate is the same date by name, in every one of those four states:
    // it is the field addendum 2 §7 names an attached file after.
    QCOMPARE(beforeState.dueDate, closes);
    QCOMPARE(farState.dueDate, closes);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), inside).dueDate, closes);
    QCOMPARE(computeCertificateState(certificate, {}, AlertThresholds(), after).dueDate, closes);
}

QTEST_APPLESS_MAIN(TestCertificateStateSurvey)
#include "tst_CertificateStateSurvey.moc"
