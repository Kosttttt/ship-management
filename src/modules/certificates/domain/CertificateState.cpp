#include "modules/certificates/domain/CertificateState.h"

#include "modules/certificates/domain/SurveySchedule.h"

#include <QCoreApplication>

namespace {

QString formatDate(const QDate& date)
{
    return date.toString(QStringLiteral("dd MMM yyyy"));
}

QString tr(const char* text)
{
    return QCoreApplication::translate("CertificateState", text);
}

int rank(SurveySeverity severity)
{
    switch (severity) {
    case SurveySeverity::NotRequired:
        return 0;
    case SurveySeverity::NotDue:
        return 1;
    case SurveySeverity::DueSoon:
        return 2;
    case SurveySeverity::InWindow:
        return 3;
    case SurveySeverity::Overdue:
        return 4;
    }
    return 0;
}

ExpirySeverity expirySeverityFor(QDate expiry, QDate today, const AlertThresholds& thresholds)
{
    if (today > expiry) {
        return ExpirySeverity::Expired;
    }
    const int days = today.daysTo(expiry);
    if (days <= thresholds.criticalDays) {
        return ExpirySeverity::Critical;
    }
    if (days <= thresholds.expiringSoonDays) {
        return ExpirySeverity::ExpiringSoon;
    }
    return ExpirySeverity::Valid;
}

// §4.3's table, worst first.
DisplayStatus displayFor(ExpirySeverity expiry, SurveySeverity survey)
{
    if (expiry == ExpirySeverity::Expired) {
        return DisplayStatus::Expired;
    }
    if (survey == SurveySeverity::Overdue) {
        return DisplayStatus::SurveyOverdue;
    }
    if (expiry == ExpirySeverity::Critical) {
        return DisplayStatus::Critical;
    }
    if (expiry == ExpirySeverity::ExpiringSoon) {
        return DisplayStatus::ExpiringSoon;
    }
    if (survey == SurveySeverity::InWindow) {
        return DisplayStatus::SurveyDue;
    }
    if (survey == SurveySeverity::DueSoon) {
        return DisplayStatus::DueSoon;
    }
    return DisplayStatus::Valid;
}

QString describe(const CertificateState& state, const Certificate& certificate, QDate today)
{
    if (state.expiry == ExpirySeverity::Expired) {
        return tr("Expired on %1.").arg(formatDate(certificate.expiryDate));
    }
    switch (state.survey) {
    case SurveySeverity::Overdue:
        return tr("Survey overdue: the window for %1 closed on %2.")
            .arg(formatDate(state.nextAnniversary), formatDate(state.windowCloses));
    case SurveySeverity::InWindow:
        return tr("Survey due: the window for %1 is open until %2.")
            .arg(formatDate(state.nextAnniversary), formatDate(state.windowCloses));
    case SurveySeverity::DueSoon:
        return tr("Survey window for %1 opens on %2.")
            .arg(formatDate(state.nextAnniversary), formatDate(state.windowOpens));
    default:
        break;
    }
    if (state.expiry == ExpirySeverity::Critical
        || state.expiry == ExpirySeverity::ExpiringSoon) {
        return tr("Expires on %1, in %2 days.")
            .arg(formatDate(certificate.expiryDate))
            .arg(today.daysTo(certificate.expiryDate));
    }
    return tr("Valid until %1.").arg(formatDate(certificate.expiryDate));
}

} // namespace

CertificateState computeCertificateState(const Certificate&          certificate,
                                         const QList<Endorsement>&   endorsements,
                                         const AlertThresholds&      thresholds,
                                         QDate                       today,
                                         const std::optional<QDate>& activeExtensionExpiry)
{
    CertificateState state;

    // A plain passthrough, set whether or not there is one, so the UI can ask
    // this one question instead of looking the extension up again
    // (certificate-extension-spec §6).
    state.activeExtensionExpiry = activeExtensionExpiry;

    // certificate-extension-spec §2: while a short-term extension is active it
    // takes over what the list shows and alerts on, and the certificate's own
    // dates are left completely alone — not read here, not written anywhere.
    //
    // Checked before the neverExpires() guard below, so that an extension
    // always governs when one exists. The UI will not start an extension
    // against a certificate that never expires (§7) and start() has no reason
    // to care, but if such a row ever arrives — a future sync, a direct write
    // — honouring it is the reading §2 asks for: the extension is the document
    // in force right now.
    if (activeExtensionExpiry.has_value()) {
        const QDate extensionExpiry = *activeExtensionExpiry;

        // The same threshold logic the certificate's own expiry goes through,
        // against the extension's date instead. No new severity tier (§2).
        state.expiry = expirySeverityFor(extensionExpiry, today, thresholds);

        // Survey evaluation is genuinely suppressed, not merely hidden: this
        // is the identical path a certificate with no survey requirement
        // already takes, so no new SurveySeverity value is needed. The window
        // dates stay invalid, which the list already renders as blank cells.
        state.survey         = SurveySeverity::NotRequired;
        state.nextSurveyType = SurveyType::Renewal;
        state.daysLeft       = today.daysTo(extensionExpiry);

        state.display = displayFor(state.expiry, state.survey);
        state.isValid = state.expiry != ExpirySeverity::Expired;

        state.reason = (state.expiry == ExpirySeverity::Expired)
                           ? tr("Short-term extension expired on %1.")
                                 .arg(formatDate(extensionExpiry))
                           : tr("Short-term extension active until %1, in %2 days.")
                                 .arg(formatDate(extensionExpiry))
                                 .arg(state.daysLeft);
        return state;
    }

    // §4: a certificate with no expiry has no anniversaries to schedule a
    // survey against, so the calculation stops here without touching §4.5.
    if (certificate.neverExpires()) {
        state.expiry  = ExpirySeverity::Valid;
        state.survey  = SurveySeverity::NotRequired;
        state.display = DisplayStatus::Valid;
        state.isValid = true;
        state.reason  = tr("Does not expire; no survey required.");
        return state;
    }

    state.expiry = expirySeverityFor(certificate.expiryDate, today, thresholds);

    const QList<QDate> anniversaries = SurveySchedule::anniversariesOf(certificate);
    QStringList        lateNotes;

    SurveySchedule::TrackState annual;
    if (certificate.requiresAnnualSurvey) {
        annual = SurveySchedule::evaluateAnnual(certificate, anniversaries, endorsements, today,
                                                thresholds.dueSoonDays, &lateNotes);
    }

    SurveySchedule::TrackState intermediate;
    if (certificate.requiresIntermediateSurvey) {
        intermediate = SurveySchedule::evaluateIntermediate(anniversaries, endorsements, today,
                                                            thresholds.dueSoonDays);
    }

    // §4.2: the worst of the two tracks. On a tie the annual is reported —
    // its window closes sooner (§3.4).
    const bool intermediateWorse = rank(intermediate.severity) > rank(annual.severity);
    const SurveySchedule::TrackState& reported = intermediateWorse ? intermediate : annual;

    state.survey = reported.severity;
    if (reported.outstanding) {
        state.nextSurveyType  = intermediateWorse ? SurveyType::Intermediate : SurveyType::Annual;
        state.nextAnniversary = reported.anniversary;
        state.windowOpens     = reported.window.opens;
        state.windowCloses    = reported.window.closes;

        // §4.4: until the window opens, count to the opening; once open (or
        // past), count to the close, which goes negative when overdue.
        state.daysLeft = (today < reported.window.opens)
                             ? today.daysTo(reported.window.opens)
                             : today.daysTo(reported.window.closes);
    } else {
        // Nothing outstanding: the next action is the renewal before expiry.
        state.nextSurveyType = SurveyType::Renewal;
        state.daysLeft       = today.daysTo(certificate.expiryDate);
    }

    state.display = displayFor(state.expiry, state.survey);
    state.isValid = state.expiry != ExpirySeverity::Expired
                    && state.survey != SurveySeverity::Overdue;

    state.reason = describe(state, certificate, today);
    if (!lateNotes.isEmpty()) {
        state.reason += QLatin1Char(' ') + lateNotes.join(QLatin1Char(' '));
    }

    return state;
}
