#include "app/MainWindow.h"

#include "core/InstallationContext.h"

#include <QStatusBar>

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

MainWindow::MainWindow(const InstallationContext& installation, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(titleFor(installation));
    resize(1024, 700);

    statusBar()->showMessage(tr("Node: %1").arg(installation.nodeId()));
}
