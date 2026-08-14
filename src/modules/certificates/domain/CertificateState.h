#pragma once

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/Endorsement.h"

#include <QDate>
#include <QList>
#include <QString>

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
};

// `today` is a parameter, never QDate::currentDate() inside the function.
// That is what makes every rule testable.
CertificateState computeCertificateState(const Certificate&        certificate,
                                         const QList<Endorsement>& endorsements,
                                         const AlertThresholds&    thresholds,
                                         QDate                     today);
