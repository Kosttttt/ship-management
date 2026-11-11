#pragma once

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/CertificateState.h"

#include <QDate>
#include <QList>
#include <QString>

class AppSettingRepository;
class CertificateExtensionRepository;
class CertificateRepository;
class EndorsementRepository;

// One vessel's certificates, each with its computed state, ready to be drawn.
//
// Split out of CertificateListWidget::reload() for size
// (housekeeping-round-spec §3.2) — but into the service layer rather than
// into another ui/ helper, because that is where this work belonged all
// along. CLAUDE.md §4 rule 3: the UI layer contains no business rules. A
// widget may format and display a value; it may not decide what the value
// should be. Reading the alert thresholds, turning a core AppSetting into
// this module's AlertThresholds, looking up each certificate's endorsements
// and active extension, running computeCertificateState() and dropping the
// rows a filter excludes are all decisions about what the values are.
//
// CertificateAlertProvider already does the same gathering for the sidebar
// badge, over the whole fleet instead of one vessel, and the comments in both
// places note that the two have to agree or the badge and the list will
// disagree about the same certificate. Making them share this class is the
// obvious next move and is deliberately not attempted here: the shapes of
// the two loops differ (one vessel against every vessel, and different
// warning text), and this round is not the place to change what the badge
// counts.
class CertificateListRows
{
public:
    CertificateListRows(CertificateRepository&          repository,
                        EndorsementRepository&          endorsements,
                        CertificateExtensionRepository& extensions,
                        AppSettingRepository&           appSettings);

    // One row of the list: the certificate and what was computed about it.
    // A single list of pairs rather than two parallel lists, so a row's
    // certificate and its state cannot come apart.
    struct Row {
        Certificate      certificate;
        CertificateState state;
    };

    // `today` is a parameter, never QDate::currentDate() inside here, for the
    // same reason computeCertificateState() takes one: every rule stays
    // testable, and one reload uses one date for every row.
    //
    // Returns false only when the certificate list itself could not be read —
    // the one failure the screen reports to the user. An unreadable
    // endorsement list or extension is warned about and treated as absent,
    // because a row showing the certificate's own dates beats a screen with a
    // hole in it.
    bool build(const QString& vesselId, bool onlyNeedingAttention, QDate today,
               QList<Row>* rows);

    QString errorString() const;

private:
    CertificateRepository&          m_repository;
    EndorsementRepository&          m_endorsements;
    CertificateExtensionRepository& m_extensions;
    AppSettingRepository&           m_appSettings;
    QString                         m_errorString;
};
