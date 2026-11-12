#include "modules/certificates/ui/CertificateCategoryTabs.h"

#include <QSignalBlocker>
#include <QTabBar>

namespace {

// The words on the tabs. CertificateEditForm has its own copy of these four
// for its combo box; they are not shared yet because that combo pairs each
// label with the enum value as item data and reads them back, which is a
// different shape from this. Worth one home if a third place ever needs them.
QString labelFor(CertificateCategory category)
{
    switch (category) {
    case CertificateCategory::Unset:
        return CertificateCategoryTabs::tr("All");
    case CertificateCategory::Statutory:
        return CertificateCategoryTabs::tr("Statutory");
    case CertificateCategory::Class:
        return CertificateCategoryTabs::tr("Class");
    case CertificateCategory::Equipment:
        return CertificateCategoryTabs::tr("Equipment");
    case CertificateCategory::Other:
        return CertificateCategoryTabs::tr("Other");
    }
    return QString();
}

// The data role the category rides on, so the selected tab is read back as a
// category rather than inferred from its position.
constexpr int kCategoryRole = Qt::UserRole + 1;

} // namespace

CertificateCategoryTabs::CertificateCategoryTabs(QWidget* parent)
    : QObject(parent)
{
    m_bar = new QTabBar(parent);
    m_bar->setObjectName(QStringLiteral("certificateCategoryTabs"));
    m_bar->setExpanding(false);
    m_bar->setDrawBase(false);
    m_bar->setFocusPolicy(Qt::StrongFocus);

    for (CertificateCategory category : CertificateListRows::categoryOrder()) {
        const int index = m_bar->addTab(labelFor(category));
        m_bar->setTabData(index, static_cast<int>(category));
    }

    // §3: always opens on All. Deliberately not remembered between runs,
    // unlike the column widths — the first thing seen at start-up must never
    // hide an expired or overdue certificate behind another tab.
    m_bar->setCurrentIndex(0);

    connect(m_bar, &QTabBar::currentChanged, this, [this](int) { emit selectionChanged(); });
}

QTabBar* CertificateCategoryTabs::bar() const
{
    return m_bar;
}

CertificateCategory CertificateCategoryTabs::selected() const
{
    const QVariant data = m_bar->tabData(m_bar->currentIndex());
    if (!data.isValid()) {
        return CertificateCategory::Unset; // All, which filters nothing
    }
    return static_cast<CertificateCategory>(data.toInt());
}

bool CertificateCategoryTabs::selectAllWithoutSignal()
{
    if (m_bar->currentIndex() == 0) {
        return false;
    }

    const QSignalBlocker blocker(m_bar);
    m_bar->setCurrentIndex(0);
    return true;
}

void CertificateCategoryTabs::updateLabels(
    const QList<CertificateListRows::CategoryTally>& tallies)
{
    for (const CertificateListRows::CategoryTally& tally : tallies) {
        // Found by its category rather than by position, so a tally list in
        // an unexpected order cannot colour the wrong tab.
        for (int index = 0; index < m_bar->count(); ++index) {
            if (m_bar->tabData(index).toInt() != static_cast<int>(tally.category)) {
                continue;
            }

            m_bar->setTabText(index, QStringLiteral("%1 (%2)")
                                         .arg(labelFor(tally.category))
                                         .arg(tally.visibleCount));

            // §2: red means "something in here needs attention". An invalid
            // QColor is how QTabBar is told to go back to the palette's own
            // colour, so this sets and clears with the same call rather than
            // leaving a tab red once it has been.
            m_bar->setTabTextColor(index, tally.needsAttention ? QColor(Qt::red) : QColor());

            m_bar->setTabToolTip(index,
                                 tally.needsAttention
                                     ? tr("Something in this category needs attention.")
                                     : QString());
            break;
        }
    }
}
