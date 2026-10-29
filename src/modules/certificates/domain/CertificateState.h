#pragma once

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/Endorsement.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

// The state calculation (certificate-control-spec.md §4) — the heart of the
// module, and a pure function: no database, no widgets, no clock of its own.
//
// It lives in its own file rather than in Certificate.h because it operates
// *on* a certificate plus its endorsements; it is not part of the
// certificate's own data (certificate-endorsement-spec §4).

enum class ExpirySeverity {
    Valid,
    ExpiringSoon,
    Critical,
    Expired
};

enum class SurveySeverity {
    NotRequired,  // this certificate has no survey schedule at all
    NotDue,       // nothing outstanding, or the next window is far off
    DueSoon,      // a window opens within the "due soon" threshold
    InWindow,     // today is inside an open window
    Overdue       // a window closed with nothing to satisfy it
};

// What the grid shows: the more severe of the two, per §4.3's table.
enum class DisplayStatus {
    Valid,
    DueSoon,
    SurveyDue,
    ExpiringSoon,
    Critical,
    SurveyOverdue,
    Expired
};

// certificate-control-spec.md §4.3. Passed in rather than read from a
// constant, so a test can hand the function different numbers. The Settings
// step makes these editable; until then every call site default-constructs
// this and gets 30/60/90.
struct AlertThresholds {
    int criticalDays     = 30;
    int expiringSoonDays = 60;
    int dueSoonDays      = 90;
};

struct CertificateState {
    ExpirySeverity expiry  = ExpirySeverity::Valid;
    SurveySeverity survey  = SurveySeverity::NotRequired;
    DisplayStatus  display = DisplayStatus::Valid;

    // False if expired OR any survey is overdue (§4.1).
    bool isValid = true;

    // The survey that needs attention first. Renewal once every anniversary
    // is satisfied and only the renewal remains (§4.5).
    SurveyType nextSurveyType = SurveyType::Unset;

    QDate nextAnniversary;
    QDate windowOpens;
    QDate windowCloses;

    // §4.4: days until the window opens, or until it closes once open, or
    // until expiry when no survey is outstanding. Negative when the date it
    // counts to has already passed.
    int daysLeft = 0;

    // Human-readable, for alerts and tooltips. Also where a survey completed
    // late is recorded (§4.5), rather than on a field of its own.
    QString reason;

    // certificate-extension-spec §6: a plain passthrough of the parameter of
    // the same name, so the list can tell whether a row is under an active
    // short-term extension — and which date to show — without looking the
    // extension up a second time.
    std::optional<QDate> activeExtensionExpiry;
};

// `today` is a parameter, never QDate::currentDate() inside the function.
// That is what makes every rule testable.
//
// activeExtensionExpiry is the expiry date of the certificate's active
// short-term extension, when it has one (certificate-extension-spec §6). Just
// the date, not the whole CertificateExtension: that keeps this function's
// dependencies exactly as narrow as they were. The caller — the list widget or
// the alert provider — is the one that knows how to look an extension up.
//
// Omitting it reproduces the behaviour from before step 11 exactly, which is
// what keeps this a pure addition for every call site that has not changed.
CertificateState computeCertificateState(const Certificate&          certificate,
                                         const QList<Endorsement>&   endorsements,
                                         const AlertThresholds&      thresholds,
                                         QDate                       today,
                                         const std::optional<QDate>& activeExtensionExpiry
                                         = std::nullopt);
