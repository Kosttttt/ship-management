#pragma once

#include "app/AlertProvider.h"

class AppSettingRepository;
class CertificateExtensionRepository;
class CertificateRepository;
class EndorsementRepository;
class InstallationContext;
class VesselRepository;

// The certificates module's answer to "what needs attention, per vessel"
// (alerts-spec.md §4).
//
// Counts certificates whose DisplayStatus is anything but Valid — exactly the
// rule the certificate list's own "needs attention" filter already uses, so
// the badge and the filtered list can never disagree.
class CertificateAlertProvider : public AlertProvider
{
public:
    CertificateAlertProvider(VesselRepository&               vessels,
                             CertificateRepository&          certificates,
                             EndorsementRepository&          endorsements,
                             CertificateExtensionRepository& extensions,
                             AppSettingRepository&           appSettings,
                             const InstallationContext&      installation);

    QList<VesselAttentionCount> attentionByVessel() const override;

private:
    VesselRepository&      m_vessels;
    CertificateRepository& m_certificates;
    EndorsementRepository& m_endorsements;

    // certificate-extension-spec §3: this provider must apply an active
    // extension exactly as CertificateListWidget::reload() does. Without it
    // the badge and banner would keep counting a certificate whose survey
    // schedule is paused, and would disagree with the list the drill-down
    // opens. Not const: findActiveFor() records its own error string.
    CertificateExtensionRepository& m_extensions;

    AppSettingRepository&      m_appSettings;
    const InstallationContext& m_installation;
};
