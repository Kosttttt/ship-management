#include "modules/certificates/service/CertificateListRows.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"

#include <QDebug>

CertificateListRows::CertificateListRows(CertificateRepository&          repository,
                                         EndorsementRepository&          endorsements,
                                         CertificateExtensionRepository& extensions,
                                         AppSettingRepository&           appSettings)
    : m_repository(repository)
    , m_endorsements(endorsements)
    , m_extensions(extensions)
    , m_appSettings(appSettings)
{
}

bool CertificateListRows::build(const QString& vesselId, bool onlyNeedingAttention, QDate today,
                                QList<Row>* rows)
{
    m_errorString.clear();
    rows->clear();

    QList<Certificate> certificates;
    if (!m_repository.list(vesselId, &certificates)) {
        m_errorString = m_repository.errorString();
        return false;
    }

    // settings-app-setting-spec §7: the one place an AppSetting (core)
    // becomes an AlertThresholds (this module), translated a field at a time
    // on this side of the boundary. Core knows nothing about AlertThresholds.
    //
    // A failed read leaves the hardcoded 30/60/90 defaults in place: a list
    // that still shows something reasonable beats one that shows nothing,
    // the same call step 7 made for unreadable endorsements.
    AppSetting      setting;
    AlertThresholds thresholds;
    if (m_appSettings.read(&setting)) {
        thresholds.criticalDays     = setting.criticalDays;
        thresholds.expiringSoonDays = setting.expiringSoonDays;
        thresholds.dueSoonDays      = setting.dueSoonDays;
    } else {
        qWarning() << "Could not read alert thresholds, using defaults:"
                   << m_appSettings.errorString();
    }

    for (const Certificate& certificate : certificates) {
        // One list() per certificate: an N+1 pattern, deliberately not
        // batched at the fleet sizes this app targets
        // (certificate-list-status-spec §8).
        QList<Endorsement> endorsements;
        if (!m_endorsements.list(certificate.id, &endorsements)) {
            // A certificate whose endorsements cannot be read is still worth
            // showing; it simply looks as though it has none.
            qWarning() << "Could not load endorsements for" << certificate.name << ":"
                       << m_endorsements.errorString();
        }

        // certificate-extension-spec §3/§6: this screen is one of exactly two
        // places that must look an active extension up and hand its date to
        // the state calculation — CertificateAlertProvider is the other, and
        // the two have to agree or the badge and the list will disagree about
        // the same certificate.
        //
        // An unreadable extension is treated as "none", the same judgment
        // already made for an unreadable endorsement list: a row that still
        // shows the certificate's own dates beats a screen with a hole in it.
        std::optional<CertificateExtension> extension;
        if (!m_extensions.findActiveFor(certificate.id, &extension)) {
            qWarning() << "Could not load the short-term extension for" << certificate.name << ":"
                       << m_extensions.errorString();
        }

        const std::optional<QDate> extensionExpiry =
            extension.has_value() ? std::optional<QDate>(extension->expiryDate) : std::nullopt;

        Row row;
        row.certificate = certificate;
        row.state =
            computeCertificateState(certificate, endorsements, thresholds, today, extensionExpiry);

        // certificate-list-status-spec §7: the filter works on computed
        // severity, which is never stored, so it can only be applied here —
        // after the calculation and before the row exists.
        if (onlyNeedingAttention && row.state.display == DisplayStatus::Valid) {
            continue;
        }

        rows->append(row);
    }

    return true;
}

QString CertificateListRows::errorString() const
{
    return m_errorString;
}
