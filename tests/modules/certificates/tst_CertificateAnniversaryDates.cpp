#include "CertificateStateTestSupport.h"

#include "modules/certificates/domain/SurveySchedule.h"

#include <QtTest>

using namespace CertificateStateTestSupport;

// How the anniversary dates are worked out from a certificate's issue and
// expiry dates — anniversary-dates-fix-spec.md §5, correcting
// certificate-control-spec.md §4.5.
//
// Its own suite rather than more cases in tst_CertificateStateSurvey, which is
// already 285 lines and would go past the ~300-line guideline (CLAUDE.md §9).
// The split is the same one that put tst_CertificateStateSurveyMatching in a
// file of its own: that suite asks which anniversaries one survey accounts
// for, this one asks what the anniversaries are in the first place, and the
// original keeps the window, severity and days-left rules.
//
// Every certificate here is deliberately NOT a whole number of years long,
// which is why the shared anniversary() helper is not used: it works the term
// out from the difference of the two years, the very thing the fix is about.
// The dates are written out literally instead, so a test can never agree with
// the code about a wrong answer.
class TestCertificateAnniversaryDates : public QObject
{
    Q_OBJECT

private slots:
    void theSmcCaseReportedItsIntermediateWindowAYearLate();
    void theDocCaseShowedADueAnnualSurveyAsValid();
    void anExactFiveYearTermIsUnchanged();
    void aRenewalIssuedBeforeItsTermStartsHasNoAnniversaryThatYear();
    void aCertificateIssuedAfterAnExtensionKeepsItsFirstAnniversary();
    void sixMonthsRoundsTheTermUpAndFiveMonthsRoundsItDown();
    void a29FebruaryExpiryClampsToTheLastDayOfTheMonth();
    void aShortTermCertificateHasNoAnniversaries();
};

namespace {

// The shared builder, with the issue date put back to what it really is
// instead of a whole number of years before expiry.
Certificate certificateFrom(const QDate& issue, const QDate& expiry)
{
    Certificate certificate = certificateExpiring(expiry);
    certificate.issueDate   = issue;
    return certificate;
}

} // namespace

void TestCertificateAnniversaryDates::theSmcCaseReportedItsIntermediateWindowAYearLate()
{
    // §1, the certificate the developer was looking at: V TRE, No. 14, S.M.C.,
    // issued 12 Jan 2026, expires 30 Dec 2030, intermediate survey only. Two
    // weeks short of five years, so the old rule called it a four-year term
    // and lost 30 Dec 2026 off the front of the list.
    Certificate smc                = certificateFrom(QDate(2026, 1, 12), QDate(2030, 12, 30));
    smc.name                       = QStringLiteral("Safety Management Certificate");
    smc.requiresAnnualSurvey       = false;
    smc.requiresIntermediateSurvey = true;

    const QList<QDate> expected{QDate(2026, 12, 30), QDate(2027, 12, 30), QDate(2028, 12, 30),
                                QDate(2029, 12, 30)};
    QCOMPARE(SurveySchedule::anniversariesOf(smc), expected);

    const CertificateState state =
        computeCertificateState(smc, {}, AlertThresholds(), QDate(2026, 11, 13));

    // The window spans the 2nd anniversary's opening to the 3rd's closing.
    QCOMPARE(state.nextSurveyType, SurveyType::Intermediate);
    QCOMPARE(state.windowOpens, QDate(2027, 9, 30));
    QCOMPARE(state.windowCloses, QDate(2029, 3, 30));

    // Exactly what was on screen, a full year late, asserted the other way
    // round as well so this test cannot pass against the old rule.
    QVERIFY2(state.windowOpens != QDate(2028, 9, 30), "the old rule's answer");
    QVERIFY2(state.windowCloses != QDate(2030, 3, 30), "the old rule's answer");
}

void TestCertificateAnniversaryDates::theDocCaseShowedADueAnnualSurveyAsValid()
{
    // §1, the worse of the two: No. 13, DOC (Office ISM), issued 16 Jan 2026,
    // expires 21 Dec 2030, annual survey, nothing endorsed yet. Four weeks
    // short of five years. Today is 13 Nov 2026, with the first annual window
    // open since 21 Sep.
    Certificate doc = certificateFrom(QDate(2026, 1, 16), QDate(2030, 12, 21));
    doc.name        = QStringLiteral("Document of Compliance");

    const QList<QDate> anniversaries = SurveySchedule::anniversariesOf(doc);
    QCOMPARE(anniversaries.size(), 4);
    QCOMPARE(anniversaries.first(), QDate(2026, 12, 21));

    const QDate            today = QDate(2026, 11, 13);
    const CertificateState state = computeCertificateState(doc, {}, AlertThresholds(), today);

    QCOMPARE(state.nextSurveyType, SurveyType::Annual);
    QCOMPARE(state.nextAnniversary, QDate(2026, 12, 21));
    QCOMPARE(state.windowOpens, QDate(2026, 9, 21));
    QCOMPARE(state.windowCloses, QDate(2027, 3, 21));

    // The survey is due now, so the Status column says Survey Due. The bug
    // showed this row as Valid with 494 days to go — a survey that was open
    // for business reported as nothing to do.
    QCOMPARE(state.survey, SurveySeverity::InWindow);
    QCOMPARE(state.display, DisplayStatus::SurveyDue);
    QVERIFY2(state.display != DisplayStatus::Valid, "the old rule's answer");
    QCOMPARE(state.daysLeft, today.daysTo(QDate(2027, 3, 21)));
    QVERIFY2(state.daysLeft != 494, "the old rule's answer");
}

void TestCertificateAnniversaryDates::anExactFiveYearTermIsUnchanged()
{
    // The ordinary case, and the one every other suite is built on: nothing
    // about it may move. Rounding to the nearest year must leave a term that
    // is already exact exactly where it was.
    const Certificate exact = certificateFrom(QDate(2025, 6, 21), QDate(2030, 6, 21));

    const QList<QDate> expected{QDate(2026, 6, 21), QDate(2027, 6, 21), QDate(2028, 6, 21),
                                QDate(2029, 6, 21)};
    QCOMPARE(SurveySchedule::anniversariesOf(exact), expected);
}

void TestCertificateAnniversaryDates::aRenewalIssuedBeforeItsTermStartsHasNoAnniversaryThatYear()
{
    // §3, the first of the two normal cases. A renewal survey done within the
    // three months before expiry produces a certificate that runs from the old
    // expiry date, so it is issued *before* its own term begins: issued
    // 1 Jun 2025, expires 21 Jun 2030. 21 Jun 2025 is where the term starts,
    // not an anniversary needing a survey — which is why the rule rounds the
    // term rather than taking every date with the expiry's day and month after
    // the issue date.
    const Certificate renewed = certificateFrom(QDate(2025, 6, 1), QDate(2030, 6, 21));

    const QList<QDate> expected{QDate(2026, 6, 21), QDate(2027, 6, 21), QDate(2028, 6, 21),
                                QDate(2029, 6, 21)};
    QCOMPARE(SurveySchedule::anniversariesOf(renewed), expected);
    QVERIFY2(!SurveySchedule::anniversariesOf(renewed).contains(QDate(2025, 6, 21)),
             "the start of the term is not an anniversary");
}

void TestCertificateAnniversaryDates::aCertificateIssuedAfterAnExtensionKeepsItsFirstAnniversary()
{
    // §3, the other normal case, and the one the old rule got wrong. A
    // certificate issued after a short-term extension starts up to three
    // months *after* the day and month of its expiry: issued 20 Mar 2026,
    // expires 30 Dec 2030, about four years nine months. 30 Dec 2026 is a
    // genuine first anniversary and the old rule threw it away.
    const Certificate afterExtension = certificateFrom(QDate(2026, 3, 20), QDate(2030, 12, 30));

    const QList<QDate> anniversaries = SurveySchedule::anniversariesOf(afterExtension);
    QCOMPARE(anniversaries.size(), 4);
    QCOMPARE(anniversaries.first(), QDate(2026, 12, 30));
}

void TestCertificateAnniversaryDates::sixMonthsRoundsTheTermUpAndFiveMonthsRoundsItDown()
{
    // §3's boundary, stated as "six months or more rounds up", tested from
    // both sides of it so the rule cannot drift into "more than six months".
    const Certificate up = certificateFrom(QDate(2026, 1, 1), QDate(2030, 7, 1)); // 4y 6m exactly
    QCOMPARE(SurveySchedule::anniversariesOf(up).size(), 4);                      // term 5
    QCOMPARE(SurveySchedule::anniversariesOf(up).first(), QDate(2026, 7, 1));

    const Certificate down = certificateFrom(QDate(2026, 2, 1), QDate(2030, 7, 1)); // 4y 5m
    QCOMPARE(SurveySchedule::anniversariesOf(down).size(), 3);                       // term 4
    QCOMPARE(SurveySchedule::anniversariesOf(down).first(), QDate(2027, 7, 1));
}

void TestCertificateAnniversaryDates::a29FebruaryExpiryClampsToTheLastDayOfTheMonth()
{
    // §3's last line, unchanged by the fix: a day the target year does not
    // have becomes that month's last day, which QDate::addYears already does.
    const Certificate leapExpiry = certificateFrom(QDate(2027, 2, 28), QDate(2032, 2, 29));

    const QList<QDate> expected{QDate(2028, 2, 29), // a leap year, so the day exists
                                QDate(2029, 2, 28), QDate(2030, 2, 28), QDate(2031, 2, 28)};
    QCOMPARE(SurveySchedule::anniversariesOf(leapExpiry), expected);
}

void TestCertificateAnniversaryDates::aShortTermCertificateHasNoAnniversaries()
{
    // Under 18 months there is no anniversary between issue and expiry, so the
    // renewal survey before expiry is the only thing outstanding — as before
    // the fix. Seventeen months is the closest such term to the boundary: six
    // months or more of a second year would round the term up to two, which
    // does have one anniversary.
    const Certificate shortTerm = certificateFrom(QDate(2026, 1, 10), QDate(2027, 6, 10));
    QVERIFY(SurveySchedule::anniversariesOf(shortTerm).isEmpty());

    const Certificate veryShort = certificateFrom(QDate(2026, 1, 10), QDate(2026, 10, 10));
    QVERIFY(SurveySchedule::anniversariesOf(veryShort).isEmpty());

    const CertificateState state =
        computeCertificateState(shortTerm, {}, AlertThresholds(), QDate(2026, 11, 13));
    QCOMPARE(state.nextSurveyType, SurveyType::Renewal);
}

QTEST_APPLESS_MAIN(TestCertificateAnniversaryDates)
#include "tst_CertificateAnniversaryDates.moc"
