#include "modules/certificates/ui/CertificateCategoryTabs.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
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

// How big the dot is, as a fraction of the label's height — so it keeps its
// proportion to the text at any font size or display scaling instead of
// being a fixed number of pixels that looks wrong at one of them.
constexpr int kDotDivisor = 2;
constexpr int kDotMinimum = 8;

int dotSizeFor(const QTabBar& bar)
{
    return qMax(kDotMinimum, bar.fontMetrics().height() / kDotDivisor);
}

// The mark on a tab that needs attention (§6): a small filled red circle,
// drawn here with QPainter rather than loaded from a file, which needs no
// resource, no path to get wrong and no deployment step.
//
// An icon and not a text colour, which is what the first build used. On this
// project's Windows 11 machine the default style paints tab labels in its own
// colours and ignores QTabBar::setTabTextColor() entirely, so the mark was
// stored, reported by the tooltip, asserted by the tests — and never drawn.
// Tab icons are drawn by every style. A dot also does not rely on colour
// alone, which helps anyone who does not distinguish red easily.
QIcon makeAttentionDot(const QTabBar& bar)
{
    const int   logicalSize = dotSizeFor(bar);
    const qreal ratio       = bar.devicePixelRatioF();

    QPixmap pixmap(QSize(logicalSize, logicalSize) * ratio);
    pixmap.fill(Qt::transparent);
    // Set before the painter is made, so drawing below is in logical units
    // and comes out sharp on a scaled display rather than enlarged and soft.
    pixmap.setDevicePixelRatio(ratio);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Qt::red));
    // Inset by half a pixel so the antialiased edge has somewhere to go and
    // the circle is not clipped flat on its sides.
    painter.drawEllipse(QRectF(0.5, 0.5, logicalSize - 1.0, logicalSize - 1.0));
    painter.end();

    return QIcon(pixmap);
}

} // namespace

CertificateCategoryTabs::CertificateCategoryTabs(QWidget* parent)
    : QObject(parent)
{
    m_bar = new QTabBar(parent);
    m_bar->setObjectName(QStringLiteral("certificateCategoryTabs"));
    m_bar->setExpanding(false);
    m_bar->setDrawBase(false);
    m_bar->setFocusPolicy(Qt::StrongFocus);

    // Built once, from this bar's own font and device pixel ratio, and the
    // bar's icon size is set to match so the style draws the dot at its
    // natural size instead of scaling it to the default 16 by 16.
    m_attentionMark = makeAttentionDot(*m_bar);
    const int dotSize = dotSizeFor(*m_bar);
    m_bar->setIconSize(QSize(dotSize, dotSize));

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

            // §2 and §6: a red dot means "something in here needs attention".
            // A null QIcon is how a tab is told to have none, so this sets
            // and clears with the same call rather than leaving a tab marked
            // once it has been.
            m_bar->setTabIcon(index, tally.needsAttention ? m_attentionMark : QIcon());

            m_bar->setTabToolTip(index,
                                 tally.needsAttention
                                     ? tr("Something in this category needs attention.")
                                     : QString());
            break;
        }
    }
}
