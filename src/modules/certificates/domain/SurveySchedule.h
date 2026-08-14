#pragma once

#include "modules/certificates/domain/CertificateState.h"

// The survey calendar a certificate's dates imply, and which of its surveys
// are outstanding (certificate-control-spec.md §3.2, §4.5).
//
// Split out of CertificateState.cpp so each file stays readable: this answers
// "what does the schedule say", and CertificateState answers "how severe is
// that, and how do we word it".
namespace SurveySchedule {

struct Window {
    QDate opens;
    QDate closes;

    // Boundaries are inclusive (§4.8 item 10).
    bool contains(const QDate& date) const;
};

// What one track — annual or intermediate — currently needs.
struct TrackState {
    SurveySeverity severity = SurveySeverity::NotRequired;
    QDate          anniversary;
    Window         window;
    bool           outstanding = false;
};

// Earliest first, so index 0 is the 1st anniversary (§4.5).
QList<QDate> anniversariesOf(const Certificate& certificate);

Window annualWindow(const QDate& anniversary);

// Fills lateNotes with a line per anniversary satisfied after its window had
// already closed (§4.5's late-endorsement rule).
TrackState evaluateAnnual(const Certificate&        certificate,
                          const QList<QDate>&       anniversaries,
                          const QList<Endorsement>& endorsements,
                          QDate                     today,
                          int                       dueSoonDays,
                          QStringList*              lateNotes);

TrackState evaluateIntermediate(const QList<QDate>&       anniversaries,
                                const QList<Endorsement>& endorsements,
                                QDate                     today,
                                int                       dueSoonDays);

} // namespace SurveySchedule
