#pragma once

#include <QMainWindow>

class AppSettingRepository;
class CertificateListWidget;
class InstallationContext;
class ModuleRegistry;
class VesselListWidget;
class VesselRepository;
class QComboBox;
class QListWidget;
class QStackedWidget;

// The application shell: a sidebar on the left, the selected screen on the
// right, and — in OFFICE mode only — a vessel selector in the toolbar
// (certificate-crud-spec §3).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const InstallationContext& installation,
               VesselRepository&          vessels,
               AppSettingRepository&      appSettings,
               ModuleRegistry&            modules,
               QWidget*                   parent = nullptr);

private:
    void buildSidebar(const InstallationContext& installation,
                      VesselRepository&          vessels,
                      AppSettingRepository&      appSettings,
                      ModuleRegistry&            modules);
    void buildVesselSelector();

    // Re-reads the fleet into the selector, keeping the current selection by
    // id. By id rather than by position, because inserting a vessel shifts
    // where the others sit alphabetically (certificate-crud-spec §8.1).
    void refreshVesselSelector();

    VesselRepository* m_vesselRepository = nullptr;

    QListWidget*    m_sidebar        = nullptr;
    QStackedWidget* m_pages          = nullptr;
    QComboBox*      m_vesselSelector = nullptr;

    // OFFICE only, and only so the selector can be told when the fleet
    // changes underneath it.
    VesselListWidget* m_vesselList = nullptr;

    // OFFICE only. MainWindow drives the certificates screen directly rather
    // than through a general "vessel-scoped module" abstraction — with exactly
    // one per-vessel module, that abstraction would be solving a problem that
    // does not exist yet (certificate-crud-spec §3). The moment a second one
    // appears, this is the line to generalise.
    CertificateListWidget* m_certificates = nullptr;
};
