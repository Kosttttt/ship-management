#include "app/AlertBanner.h"

#include "app/IModule.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AlertBanner::AlertBanner(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("alertBanner"));

    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("alertBannerFrame"));
    frame->setFrameShape(QFrame::StyledPanel);
    // The same pale amber the "needs attention" idea uses elsewhere: enough to
    // notice, not so much that it shouts over the screen behind it.
    frame->setAutoFillBackground(true);
    QPalette framePalette = frame->palette();
    framePalette.setColor(QPalette::Window, QColor(QStringLiteral("#FFF3CD")));
    framePalette.setColor(QPalette::WindowText, QColor(QStringLiteral("#664D03")));
    frame->setPalette(framePalette);

    auto* heading = new QLabel(tr("Needs attention"), frame);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* dismissButton = new QPushButton(QStringLiteral("✕"), frame);
    dismissButton->setObjectName(QStringLiteral("alertBannerDismiss"));
    dismissButton->setToolTip(tr("Dismiss"));
    dismissButton->setFlat(true);
    dismissButton->setFixedWidth(28);

    auto* headingRow = new QHBoxLayout;
    headingRow->addWidget(heading);
    headingRow->addStretch();
    headingRow->addWidget(dismissButton);

    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);

    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->addLayout(headingRow);
    frameLayout->addLayout(m_rowsLayout);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(frame);

    connect(dismissButton, &QPushButton::clicked, this, &AlertBanner::dismissed);

    // Hidden until there is something to say.
    hide();
}

void AlertBanner::clearRows()
{
    while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void AlertBanner::populate(const QList<AlertBannerEntry>& entries)
{
    if (entries.isEmpty()) {
        return; // nothing to say, so nothing to show
    }

    clearRows();

    for (const AlertBannerEntry& entry : entries) {
        auto* row       = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* vesselLabel = new QLabel(entry.vesselName, row);
        QFont vesselFont  = vesselLabel->font();
        vesselFont.setBold(true);
        vesselLabel->setFont(vesselFont);

        // "certificates needing attention: 2". The module names its own
        // domain, so this sentence needs no knowledge of what a certificate
        // is — and phrasing it this way sidesteps subject-verb agreement,
        // which cannot be got right for an arbitrary module's display name
        // (there is no way to singularise "Certificates" generically).
        const QString what =
            entry.module == nullptr ? tr("items") : entry.module->displayName().toLower();
        auto* countLabel =
            new QLabel(tr("%1 needing attention: %2").arg(what, QString::number(entry.count)), row);

        auto* viewButton = new QPushButton(tr("View"), row);
        viewButton->setObjectName(QStringLiteral("alertBannerView"));

        rowLayout->addWidget(vesselLabel);
        rowLayout->addWidget(countLabel);
        rowLayout->addStretch();
        rowLayout->addWidget(viewButton);

        IModule*      module   = entry.module;
        const QString vesselId = entry.vesselId;
        connect(viewButton, &QPushButton::clicked, this,
                [this, module, vesselId]() { emit viewRequested(module, vesselId); });

        m_rowsLayout->addWidget(row);
    }

    show();
}
