#include "CertificateStateTestSupport.h"

#include <QtTest>

using namespace CertificateStateTestSupport;

// How a recorded survey is matched to an anniversary —
// survey-latest-counts-spec.md, plus certificate-control-spec.md §4.5.
//
// Split out of tst_CertificateStateSurvey for file size
// (housekeeping-round-spec §4). That suite keeps the window, severity and
// days-left rules; these five cases are all about which anniversaries one
// survey accounts for, which is the question the latest-survey round changed.
class TestCertificateStateSurveyMatching : public QObject
{
    Q_OBJECT

private slots:
    void theLatestSurveyCoversEveryAnniversaryBeforeIt();
    void theRealLoadLineCaseWithOnlyTheLatestSurveyRecorded();
    void onlyTheLatestSurveyIsReportedLate();
    void aMissedEarlierAnnualIsNotReportedOnceALaterOneExists();
    void anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing();
};

void TestCertificateStateSurveyMatching::theLatestSurveyCoversEveryAnniversaryBeforeIt()
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

void TestCertificateStateSurveyMatching::theRealLoadLineCaseWithOnlyTheLatestSurveyRecorded()
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

void TestCertificateStateSurveyMatching::onlyTheLatestSurveyIsReportedLate()
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

void TestCertificateStateSurveyMatching::aMissedEarlierAnnualIsNotReportedOnceALaterOneExists()
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

void TestCertificateStateSurveyMatching::anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing()
{
    const Certificate certificate = certificateExpiring(QDate(2031, 2, 4));
    const QDate       tooEarly    = windowOpens(anniversary(certificate, 1)).addDays(-1);

    const CertificateState state = computeCertificateState(
        certificate, {endorsementOn(tooEarly, SurveyType::Annual)}, AlertThresholds(),
        windowCloses(anniversary(certificate, 1)).addDays(1));

    QCOMPARE(state.survey, SurveySeverity::Overdue);
    QCOMPARE(state.nextAnniversary, anniversary(certificate, 1));
}

QTEST_APPLESS_MAIN(TestCertificateStateSurveyMatching)
#include "tst_CertificateStateSurveyMatching.moc"
