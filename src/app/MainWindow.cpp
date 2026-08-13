#include "app/MainWindow.h"

#include "app/IModule.h"
#include "app/ModuleRegistry.h"
#include "app/VesselDetailPage.h"
#include "app/VesselListWidget.h"
#include "core/InstallationContext.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>

namespace {

// first-run-wizard-spec §9: the mode is permanent, so it is set once at
// construction and never updated. Showing it at all times means a wrong choice
// at first run is noticed immediately rather than after data has been entered.
QString titleFor(const InstallationContext& installation)
{
    if (installation.mode() == InstallationMode::Vessel) {
        return MainWindow::tr("Ship Management System — VESSEL: %1 (%2)")
            .arg(installation.vesselName(), installation.vesselImoNumber());
    }
    return MainWindow::tr("Ship Management System — OFFICE");
}

} // namespace

MainWindow::MainWindow(const InstallationContext& installation,
                       VesselRepository&          vessels,
                       ModuleRegistry&            modules,
                       QWidget*                   parent)
    : QMainWindow(parent)
{
    setWindowTitle(titleFor(installation));
    resize(1100, 720);

    m_vesselRepository = &vessels;

    buildSidebar(installation, vessels, modules);

    // CLAUDE.md §3: the ship selector is shown at the office and hidden on a
    // vessel, which has only itself.
    if (installation.mode() == InstallationMode::Office) {
        buildVesselSelector();
    }

    statusBar()->showMessage(tr("Node: %1").arg(installation.nodeId()));
}

void MainWindow::buildSidebar(const InstallationContext& installation,
                              VesselRepository&          vessels,
                              ModuleRegistry&            modules)
{
    m_sidebar = new QListWidget(this);
    m_sidebar->setObjectName(QStringLiteral("navigationSidebar"));
    m_sidebar->setMaximumWidth(200);
    m_sidebar->setMinimumWidth(140);

    m_pages = new QStackedWidget(this);

    // "Vessels" is fixed and always first. It is core, not a module
    // (CLAUDE.md §5), so it is present whatever feature modules exist.
    m_sidebar->addItem(tr("Vessels"));
    if (installation.mode() == InstallationMode::Vessel) {
        m_pages->addWidget(new VesselDetailPage(vessels, installation, this));
    } else {
        m_vesselList = new VesselListWidget(vessels, this);
        m_pages->addWidget(m_vesselList);
    }

    // One row per registered module. Adding a module changes nothing here.
    for (IModule* module : modules.modules()) {
        auto* item = new QListWidgetItem(module->icon(), module->displayName());
        m_sidebar->addItem(item);

        QWidget* screen = module->createMainWidget(this);
        m_pages->addWidget(screen);

        // The one piece of direct wiring the spec sanctions: remember the
        // certificates screen so the vessel selector can rescope it.
        if (auto* certificates = qobject_cast<CertificateListWidget*>(screen)) {
            m_certificates = certificates;
        }
    }

    connect(m_sidebar, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_sidebar->setCurrentRow(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_pages);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);
}

void MainWindow::buildVesselSelector()
{
    auto* toolBar = addToolBar(tr("Vessel"));
    toolBar->setObjectName(QStringLiteral("vesselToolBar"));
    toolBar->setMovable(false);
    toolBar->addWidget(new QLabel(tr("Vessel: "), toolBar));

    m_vesselSelector = new QComboBox(toolBar);
    m_vesselSelector->setObjectName(QStringLiteral("vesselSelector"));
    m_vesselSelector->setMinimumWidth(260);
    m_vesselSelector->setPlaceholderText(tr("Select a vessel…"));

    toolBar->addWidget(m_vesselSelector);

    connect(m_vesselSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_certificates == nullptr) {
            return;
        }
        const QString vesselId =
            (index < 0) ? QString() : m_vesselSelector->itemData(index).toString();
        m_certificates->setVesselId(vesselId);
    });

    // The fleet can change while the application is running, and the selector
    // has to hear about it — the defect in certificate-crud-spec §8.1 was that
    // nothing told it.
    if (m_vesselList != nullptr) {
        connect(m_vesselList, &VesselListWidget::vesselsChanged,
                this, &MainWindow::refreshVesselSelector);
    }

    refreshVesselSelector();
}

void MainWindow::refreshVesselSelector()
{
    if (m_vesselSelector == nullptr || m_vesselRepository == nullptr) {
        return;
    }

    const QString selectedId = (m_vesselSelector->currentIndex() < 0)
                                   ? QString()
                                   : m_vesselSelector->currentData().toString();

    QList<Vessel> fleet;
    if (!m_vesselRepository->list(&fleet)) {
        qWarning() << "Could not populate the vessel selector:"
                   << m_vesselRepository->errorString();
        return;
    }

    int restoredIndex = -1;
    {
        // Rebuilding the entries would otherwise fire currentIndexChanged
        // several times and send the certificates screen through the prompt
        // state and back on every refresh.
        const QSignalBlocker blocker(m_vesselSelector);

        m_vesselSelector->clear();
        for (const Vessel& vessel : fleet) {
            m_vesselSelector->addItem(tr("%1 (%2)").arg(vessel.name, vessel.imoNumber),
                                      vessel.id);
        }

        // By id, not by position: a new vessel can push the previous selection
        // further down the alphabetical list.
        //
        // Nothing preselected when there was no selection: opening
        // Certificates before choosing a vessel shows an explicit prompt
        // rather than one vessel's data picked on the user's behalf
        // (certificate-crud-spec §3).
        restoredIndex = selectedId.isEmpty() ? -1 : m_vesselSelector->findData(selectedId);
        m_vesselSelector->setCurrentIndex(restoredIndex);
    }

    // Signals were blocked above, so the certificates screen is re-pointed
    // explicitly. This also covers a selection that no longer exists.
    if (m_certificates != nullptr) {
        m_certificates->setVesselId(restoredIndex < 0 ? QString() : selectedId);
    }
}
