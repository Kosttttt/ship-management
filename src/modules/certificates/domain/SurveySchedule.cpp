#include "modules/certificates/domain/SurveySchedule.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

constexpr int kWindowMonths = 3; // certificate-control-spec.md §3.2

QString formatDate(const QDate& date)
{
    return date.toString(QStringLiteral("dd MMM yyyy"));
}

// The certificate's term: issue to expiry in whole years, rounded to the
// NEAREST year — six months or more of a part year rounds up
// (certificate-control-spec.md §4.5, as corrected by
// docs/anniversary-dates-fix-spec.md).
//
// Rounding down, which is what this did until v1.0.1, threw away the first
// anniversary of every certificate issued a little after the day and month of
// its expiry. An S.M.C. issued 12 Jan 2026 and expiring 30 Dec 2030 is two
// weeks short of five years; counted as a four-year term, its first
// anniversary (30 Dec 2026) simply vanished and every later one sat one place
// too early in the list, so the intermediate window was reported a year late.
//
// Rounding is what gets both of the normal cases right. A renewal done up to
// three months early runs from the old expiry date, so the certificate is
// issued *before* its term starts (issue 1 Jun 2025, expiry 21 Jun 2030):
// that is still a five-year term, and 21 Jun 2025 is where it begins, not an
// anniversary. A certificate issued late, or after a short-term extension,
// starts up to three months *after* that day and month (issue 20 Mar 2026,
// expiry 30 Dec 2030): also a five-year term, whose first anniversary is
// 30 Dec 2026. Taking every date with the expiry's day and month after the
// issue date would invent one for the first case; rounding down loses one for
// the second.
int termInYears(const Certificate& certificate)
{
    // The whole years first: the largest n for which issue + n years is still
    // on or before expiry.
    int years = certificate.expiryDate.year() - certificate.issueDate.year();
    if (certificate.expiryDate < certificate.issueDate.addYears(years)) {
        --years;
    }

    // Then the part year left over. Six months or more of it rounds the term
    // up, which is the whole of the fix. Asked by comparing against the date
    // six months on rather than by counting days, so it stays a calendar
    // question: months are not all the same length, and QDate::addMonths
    // clamps a day the target month does not have, exactly as §4.5 wants.
    if (certificate.expiryDate >= certificate.issueDate.addYears(years).addMonths(6)) {
        ++years;
    }

    return years;
}

QList<Endorsement> ofType(const QList<Endorsement>& endorsements, SurveyType type)
{
    QList<Endorsement> matching;
    for (const Endorsement& endorsement : endorsements) {
        if (endorsement.surveyType == type && endorsement.endorsementDate.isValid()) {
            matching.append(endorsement);
        }
    }
    std::sort(matching.begin(), matching.end(),
              [](const Endorsement& a, const Endorsement& b) {
                  return a.endorsementDate < b.endorsementDate;
              });
    return matching;
}

SurveySeverity severityFor(const SurveySchedule::Window& window, QDate today, int dueSoonDays)
{
    if (today > window.closes) {
        return SurveySeverity::Overdue;
    }
    if (today >= window.opens) {
        return SurveySeverity::InWindow;
    }
    return (today.daysTo(window.opens) <= dueSoonDays) ? SurveySeverity::DueSoon
                                                       : SurveySeverity::NotDue;
}

} // namespace

bool SurveySchedule::Window::contains(const QDate& date) const
{
    return date >= opens && date <= closes;
}

QList<QDate> SurveySchedule::anniversariesOf(const Certificate& certificate)
{
    // §4.5: expiryDate minus n years, for n = totalYears-1 down to 1.
    // QDate::addYears and addMonths already clamp a day that does not exist in
    // the target month to that month's last day, which is exactly what §4.5
    // asks for — 29 February in a non-leap year becomes 28 February.
    QList<QDate> anniversaries;
    for (int n = termInYears(certificate) - 1; n >= 1; --n) {
        anniversaries.append(certificate.expiryDate.addYears(-n));
    }
    return anniversaries;
}

SurveySchedule::Window SurveySchedule::annualWindow(const QDate& anniversary)
{
    return {anniversary.addMonths(-kWindowMonths), anniversary.addMonths(kWindowMonths)};
}

SurveySchedule::TrackState SurveySchedule::evaluateAnnual(const Certificate&        certificate,
                                                          const QList<QDate>&       anniversaries,
                                                          const QList<Endorsement>& endorsements,
                                                          QDate                     today,
                                                          int                       dueSoonDays,
                                                          QStringList*              lateNotes)
{
    QList<Endorsement> candidates = ofType(endorsements, SurveyType::Annual);

    // §4.5: under REPLACES_ANNUAL an intermediate endorsement falling inside
    // the 2nd or 3rd annual window also satisfies that annual.
    if (certificate.intermediateMode == IntermediateMode::ReplacesAnnual) {
        for (const Endorsement& endorsement : ofType(endorsements, SurveyType::Intermediate)) {
            for (int index = 1; index <= 2 && index < anniversaries.size(); ++index) {
                if (annualWindow(anniversaries.at(index)).contains(endorsement.endorsementDate)) {
                    candidates.append(endorsement);
                    break;
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Endorsement& a, const Endorsement& b) {
                      return a.endorsementDate < b.endorsementDate;
                  });
    }

    // survey-latest-counts-spec.md §2: the latest annual survey covers its own
    // anniversary and every anniversary before it, so the schedule is read
    // forward from that one survey — which is what the module spec's §3.3
    // always said it did.
    //
    // This replaces an earlier claim-walk, which went through the anniversaries
    // in order and gave each the earliest endorsement dated on or after its own
    // window opened. That asked the wrong question of the data: this office
    // records only the last annual survey (the company's S-14 report), so a
    // lone recorded survey was claimed by the oldest anniversary and a later
    // one was reported overdue on a certificate that was in fact up to date.
    //
    // The accepted trade-off is that a genuinely missed earlier annual is no
    // longer flagged once a later one has been recorded. That is the
    // developer's decision, written out in §2 of that spec, not an oversight.
    int nextIndex = 0;

    if (!candidates.isEmpty()) {
        // Sorted by date throughout, so the last one is the latest survey.
        const QDate latest = candidates.last().endorsementDate;

        // The last anniversary whose window had opened by the day that survey
        // was done; everything up to and including it is covered. A survey
        // dated before even the first window opens covers nothing and leaves
        // this at -1, which is the one case the old walk already got right.
        int covered = -1;
        for (int index = 0; index < anniversaries.size(); ++index) {
            if (annualWindow(anniversaries.at(index)).opens > latest) {
                break; // ascending, so no later window had opened either
            }
            covered = index;
        }

        if (covered >= 0) {
            // Late for the anniversary this survey actually covers, and only
            // for that one. The earlier anniversaries it carries have no survey
            // of their own, so there is no date to call them late from.
            if (latest > annualWindow(anniversaries.at(covered)).closes) {
                lateNotes->append(
                    QCoreApplication::translate("CertificateState",
                                                "The %1 survey was completed late, on %2.")
                        .arg(formatDate(anniversaries.at(covered)), formatDate(latest)));
            }
            nextIndex = covered + 1;
        }
    }

    TrackState track;

    // Past the last anniversary — or a term too short to have any — leaves only
    // the renewal survey before expiry (§4.5).
    if (nextIndex >= anniversaries.size()) {
        track.severity = SurveySeverity::NotDue;
        return track;
    }

    const QDate  required = anniversaries.at(nextIndex);
    const Window window   = annualWindow(required);

    track.severity    = severityFor(window, today, dueSoonDays);
    track.anniversary = required;
    track.window      = window;
    track.outstanding = true;
    return track;
}

SurveySchedule::TrackState SurveySchedule::evaluateIntermediate(
    const QList<QDate>&       anniversaries,
    const QList<Endorsement>& endorsements,
    QDate                     today,
    int                       dueSoonDays)
{
    TrackState track;

    // The window is anchored to the 2nd and 3rd anniversaries, so a term too
    // short to have them has nowhere to put an intermediate survey.
    if (anniversaries.size() < 3) {
        return track;
    }

    const Window window{annualWindow(anniversaries.at(1)).opens,
                        annualWindow(anniversaries.at(2)).closes};

    for (const Endorsement& endorsement : ofType(endorsements, SurveyType::Intermediate)) {
        // Same "late still counts" rule as the annual track.
        if (endorsement.endorsementDate >= window.opens) {
            track.severity = SurveySeverity::NotDue;
            return track;
        }
    }

    track.severity    = severityFor(window, today, dueSoonDays);
    track.anniversary = anniversaries.at(2);
    track.window      = window;
    track.outstanding = true;
    return track;
}
