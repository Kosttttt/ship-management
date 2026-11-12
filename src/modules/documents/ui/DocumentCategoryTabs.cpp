#include "modules/documents/ui/DocumentCategoryTabs.h"

#include <QTabBar>

namespace {

// The data role the category rides on, so the selected tab is read back as a
// category rather than inferred from its position.
constexpr int kCategoryRole = Qt::UserRole + 1;

QString labelFor(DocumentCategory category)
{
    switch (category) {
    case DocumentCategory::Unset:
        return DocumentCategoryTabs::tr("All");
    case DocumentCategory::Sms:
        return DocumentCategoryTabs::tr("SMS");
    case DocumentCategory::Circular:
        return DocumentCategoryTabs::tr("Circulars");
    case DocumentCategory::PlanManual:
        return DocumentCategoryTabs::tr("Plans && manuals");
    case DocumentCategory::Report:
        return DocumentCategoryTabs::tr("Reports");
    }
    return QString();
}

} // namespace

QList<DocumentCategory> DocumentCategoryTabs::order()
{
    return {DocumentCategory::Unset, DocumentCategory::Sms, DocumentCategory::Circular,
            DocumentCategory::PlanManual, DocumentCategory::Report};
}

DocumentCategoryTabs::DocumentCategoryTabs(QWidget* parent)
    : QObject(parent)
{
    m_bar = new QTabBar(parent);
    m_bar->setObjectName(QStringLiteral("documentCategoryTabs"));
    m_bar->setExpanding(false);
    m_bar->setDrawBase(false);

    for (DocumentCategory category : order()) {
        const int index = m_bar->addTab(labelFor(category));
        m_bar->setTabData(index, static_cast<int>(category));
    }

    // Opens on All (§4.4), and not remembered between runs — the same call
    // the certificate list made, for the same reason: the first thing seen
    // should be everything, not whatever was last looked at.
    m_bar->setCurrentIndex(0);

    connect(m_bar, &QTabBar::currentChanged, this, [this](int) { emit selectionChanged(); });
}

QTabBar* DocumentCategoryTabs::bar() const
{
    return m_bar;
}

DocumentCategory DocumentCategoryTabs::selected() const
{
    const QVariant data = m_bar->tabData(m_bar->currentIndex());
    if (!data.isValid()) {
        return DocumentCategory::Unset; // All
    }
    return static_cast<DocumentCategory>(data.toInt());
}

void DocumentCategoryTabs::updateCounts(const QHash<int, int>& countsByCategory)
{
    for (int index = 0; index < m_bar->count(); ++index) {
        const int key = m_bar->tabData(index).toInt();
        m_bar->setTabText(index,
                          QStringLiteral("%1 (%2)")
                              .arg(labelFor(static_cast<DocumentCategory>(key)))
                              .arg(countsByCategory.value(key, 0)));
    }
}
