#include "modules/certificates/service/CertificateListRows.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"

#include <QDebug>

namespace {

// The tally for one category, or the All entry when asked for Unset.
//
// Null when a row carries a category that is not in the list, which no
// stored certificate can: migration 005's CHECK and the repository both
// refuse Unset. Such a row is then simply not counted rather than counted
// somewhere misleading.
CertificateListRows::CategoryTally* tallyFor(QList<CertificateListRows::CategoryTally>* tallies,
                                             CertificateCategory                        category)
{
    for (CertificateListRows::CategoryTally& tally : *tallies) {
        if (tally.category == category) {
            return &tally;
        }
    }
    return nullptr;
}

} // namespace

QList<CertificateCategory> CertificateListRows::categoryOrder()
{
    return {CertificateCategory::Unset, CertificateCategory::Statutory,
            CertificateCategory::Class, CertificateCategory::Equipment,
            CertificateCategory::Other};
}

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

bool CertificateListRows::build(const QString& vesselId, CertificateCategory category,
                                bool onlyNeedingAttention, QDate today, QList<Row>* rows,
                                QList<CategoryTally>* tallies)
{
    m_errorString.clear();
    rows->clear();

    // All five, always, and before anything can fail: a tab that vanished
    // when its category emptied would move the others under the cursor, and a
    // vessel that cannot be read should show five zeros rather than no tabs.
    tallies->clear();
    for (CertificateCategory entry : categoryOrder()) {
        CategoryTally tally;
        tally.category = entry;
        tallies->append(tally);
    }

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

        // certificate-list-status-spec §7: "needs attention" is computed
        // severity, which is never stored, so everything below can only be
        // decided here — after the calculation.
        const bool needsAttention = row.state.display != DisplayStatus::Valid;

        CategoryTally* ownTally = tallyFor(tallies, certificate.category);
        CategoryTally* allTally = tallyFor(tallies, CertificateCategory::Unset);

        // Red before both filters, and it is the *tab* filter that makes the
        // order matter: §3 wants the statutory tab red while somebody is
        // standing on Class, which only works if every certificate on the
        // vessel votes here. That is the whole point of the colour — a
        // problem in a category nobody is looking at.
        //
        // The checkbox is a different matter: being above it reads right and
        // matches §3's wording, but it changes nothing either way, because
        // the only rows the checkbox hides are the ones that do not need
        // attention and so have no vote to cast. Said here because a probe
        // moving this below the checkbox failed to break a single test, and
        // the next person to notice that deserves the reason rather than a
        // comment that overstates what the order is doing.
        if (needsAttention) {
            if (ownTally != nullptr) {
                ownTally->needsAttention = true;
            }
            allTally->needsAttention = true;
        }

        // The checkbox, which the counts do follow (§3).
        if (onlyNeedingAttention && !needsAttention) {
            continue;
        }

        // Counted before the tab filter and after the checkbox, so each tab
        // shows its own number whichever tab happens to be open.
        if (ownTally != nullptr) {
            ++ownTally->visibleCount;
        }
        ++allTally->visibleCount;

        // The selected tab. Unset means All, which filters nothing.
        if (category != CertificateCategory::Unset && certificate.category != category) {
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
