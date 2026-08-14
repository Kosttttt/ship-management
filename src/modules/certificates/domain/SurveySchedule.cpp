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

    // §4.5 as amended: an endorsement satisfies the earliest unsatisfied
    // anniversary whose window it falls on or after the opening of. There is
    // no upper bound at the window's close — a late survey is still a survey —
    // only being claimed first by an earlier open anniversary.
    //
    // Walking anniversaries and endorsements together in date order is exactly
    // that rule: each anniversary takes the earliest endorsement still
    // available to it.
    TrackState track;
    int        next = 0;

    for (const QDate& anniversary : anniversaries) {
        const Window window = annualWindow(anniversary);

        // Anything dated before this window opened can never satisfy this
        // anniversary, nor any later one, whose windows open later still.
        while (next < candidates.size()
               && candidates.at(next).endorsementDate < window.opens) {
            ++next;
        }

        if (next >= candidates.size()) {
            track.severity    = severityFor(window, today, dueSoonDays);
            track.anniversary = anniversary;
            track.window      = window;
            track.outstanding = true;
            return track;
        }

        const QDate claimed = candidates.at(next).endorsementDate;
        if (claimed > window.closes) {
            lateNotes->append(
                QCoreApplication::translate("CertificateState",
                                            "The %1 survey was completed late, on %2.")
                    .arg(formatDate(anniversary), formatDate(claimed)));
        }
        ++next;
    }

    // Every anniversary satisfied: only the renewal remains (§4.5).
    track.severity = SurveySeverity::NotDue;
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
