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

    // What one tab of the category bar has to show
    // (certificate-category-tabs-spec §4).
    struct CategoryTally {
        // Unset is the "All" entry — not a category, every category. The
        // same pun the `category` parameter of build() uses, and the only
        // value that can never belong to a stored certificate.
        CertificateCategory category = CertificateCategory::Unset;

        // How many rows this tab would show *as the list is filtered now*.
        // §3: "the counts follow what is visible", so with the attention
        // checkbox ticked, "Class (2)" means two class certificates needing
        // attention. It does not follow which tab is selected: every tab
        // shows its own number whichever one is open.
        int visibleCount = 0;

        // Whether this category holds anything needing attention on this
        // vessel — what turns a tab's text red.
        //
        // §3: red follows the vessel, not the checkbox. A certificate the
        // checkbox is hiding still votes here, because the whole point is to
        // see a problem in a category you are not looking at.
        bool needsAttention = false;
    };

    // The tab bar's entries in display order: All first, then the four real
    // categories in the order the program already uses (§3).
    //
    // build() fills its tallies in exactly this order, so the widget can put
    // them against its tabs without matching on the category.
    static QList<CertificateCategory> categoryOrder();

    // `category` is the selected tab; Unset means All and filters nothing.
    //
    // `today` is a parameter, never QDate::currentDate() inside here, for the
    // same reason computeCertificateState() takes one: every rule stays
    // testable, and one reload uses one date for every row.
    //
    // `tallies` comes back with one entry per categoryOrder() entry, counted
    // in the same pass as the rows — which is the point of it being here and
    // not in the widget: numbers worked out separately from the rows they
    // describe are numbers that will eventually disagree with them.
    //
    // Returns false only when the certificate list itself could not be read —
    // the one failure the screen reports to the user. An unreadable
    // endorsement list or extension is warned about and treated as absent,
    // because a row showing the certificate's own dates beats a screen with a
    // hole in it.
    bool build(const QString& vesselId, CertificateCategory category, bool onlyNeedingAttention,
               QDate today, QList<Row>* rows, QList<CategoryTally>* tallies);

    QString errorString() const;

private:
    CertificateRepository&          m_repository;
    EndorsementRepository&          m_endorsements;
    CertificateExtensionRepository& m_extensions;
    AppSettingRepository&           m_appSettings;
    QString                         m_errorString;
};
