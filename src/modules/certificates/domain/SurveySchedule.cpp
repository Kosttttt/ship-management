#include "modules/certificates/domain/SurveySchedule.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

constexpr int kWindowMonths = 3; // certificate-control-spec.md §3.2

QString formatDate(const QDate& date)
{
    return date.toString(QStringLiteral("dd MMM yyyy"));
}

// The whole years between issue and expiry — the certificate's term.
int termInYears(const Certificate& certificate)
{
    int years = certificate.expiryDate.year() - certificate.issueDate.year();
    if (certificate.expiryDate < certificate.issueDate.addYears(years)) {
        --years;
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
