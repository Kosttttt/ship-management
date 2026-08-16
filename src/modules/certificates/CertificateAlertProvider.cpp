#include "modules/certificates/CertificateAlertProvider.h"

#include "core/AppSettingRepository.h"
#include "core/InstallationContext.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateState.h"

#include <QDate>

CertificateAlertProvider::CertificateAlertProvider(VesselRepository&          vessels,
                                                   CertificateRepository&     certificates,
                                                   EndorsementRepository&     endorsements,
                                                   AppSettingRepository&      appSettings,
                                                   const InstallationContext& installation)
    : m_vessels(vessels)
    , m_certificates(certificates)
    , m_endorsements(endorsements)
    , m_appSettings(appSettings)
    , m_installation(installation)
{
}

QList<VesselAttentionCount> CertificateAlertProvider::attentionByVessel() const
{
    QList<VesselAttentionCount> results;

    // VesselRepository::list() is already scope-correct: the whole fleet at
    // the office, this installation's one vessel at sea. Nothing extra is
    // needed here to honour that rule (CLAUDE.md §3).
    QList<Vessel> fleet;
    if (!m_vessels.list(&fleet)) {
        qWarning() << "Alert provider could not read the fleet:" << m_vessels.errorString();
        return results;
    }

    // Same fallback the certificate list uses: a failed read leaves the
    // hardcoded 30/60/90 defaults in place rather than skipping the count
    // altogether (alerts-spec.md §4 step 2).
    AppSetting      setting;
    AlertThresholds thresholds;
    if (m_appSettings.read(&setting)) {
        thresholds.criticalDays     = setting.criticalDays;
        thresholds.expiringSoonDays = setting.expiringSoonDays;
        thresholds.dueSoonDays      = setting.dueSoonDays;
    } else {
        qWarning() << "Alert provider could not read thresholds, using defaults:"
                   << m_appSettings.errorString();
    }

    // Read once for the whole sweep, so every vessel is judged against the
    // same day — the same principle as CertificateListWidget::reload().
    const QDate today = QDate::currentDate();

    for (const Vessel& vessel : fleet) {
        QList<Certificate> certificates;
        if (!m_certificates.list(vessel.id, &certificates)) {
            qWarning() << "Alert provider could not read certificates for" << vessel.name << ":"
                       << m_certificates.errorString();
            continue;
        }

        int count = 0;
        for (const Certificate& certificate : certificates) {
            // An unreadable endorsement list is treated as "no endorsements"
            // rather than a hard failure, exactly as reload() does.
            QList<Endorsement> endorsements;
            if (!m_endorsements.list(certificate.id, &endorsements)) {
                qWarning() << "Alert provider could not read endorsements for"
                           << certificate.name << ":" << m_endorsements.errorString();
            }

            const CertificateState state =
                computeCertificateState(certificate, endorsements, thresholds, today);

            if (state.display != DisplayStatus::Valid) {
                ++count;
            }
        }

        // Only vessels with something outstanding belong in the result.
        if (count > 0) {
            results.append({vessel.id, vessel.name, count});
        }
    }

    // Order follows VesselRepository::list(), which is already alphabetical
    // and matches the toolbar selector — no extra sort needed.
    return results;
}
