#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

class AlertBanner;
class AppSettingRepository;
class CertificateListWidget;
class IModule;
class InstallationContext;
class ModuleRegistry;
class VesselListWidget;
class VesselRepository;
class QSqlDatabase;
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
    // connection and dataFolder are passed straight through to the Settings
    // page's database-backup section (database-backup-spec §3 and §6).
    MainWindow(const InstallationContext& installation,
               VesselRepository&          vessels,
               AppSettingRepository&      appSettings,
               ModuleRegistry&            modules,
               QSqlDatabase&              connection,
               const QString&             dataFolder,
               QWidget*                   parent = nullptr);

    // A line about something that happened while starting — today only the
    // automatic backup (database-backup-spec §4). Shown beside the node id
    // rather than instead of it, because both say what this copy is doing.
    void showStartupNotice(const QString& text);

private:
    void buildSidebar(const InstallationContext& installation,
                      VesselRepository&          vessels,
                      AppSettingRepository&      appSettings,
                      ModuleRegistry&            modules,
                      QSqlDatabase&              connection,
                      const QString&             dataFolder);
    void buildVesselSelector();

    // Re-reads the fleet into the selector, keeping the current selection by
    // id. By id rather than by position, because inserting a vessel shifts
    // where the others sit alphabetically (certificate-crud-spec §8.1).
    void refreshVesselSelector();

    // alerts-spec.md §8. Shows the banner only if something is outstanding
    // and it has not already been shown today.
    void showDailyAlertBanner(AppSettingRepository& appSettings);

    // alerts-spec.md §6. Rewrites each module's sidebar row with its current
    // count, or plain displayName() when nothing is outstanding.
    void refreshAlertBadges();

    // The banner's View button: switch to that module's screen, scoped and
    // filtered to the vessel in question.
    void showAttentionFor(IModule* module, const QString& vesselId);

    // Tells every module which vessel is selected and lets each decide
    // whether it cares (document-management-spec.md §3.1).
    //
    // This is what stops the shell having to name a second module's screen
    // type the way it names CertificateListWidget below. That one piece of
    // direct wiring stays as it is — moving it onto the hook is a tidy-up for
    // its own round, not part of the module that introduced the hook.
    void announceSelectedVessel(const QString& vesselId);

    VesselRepository* m_vesselRepository = nullptr;

    // The status bar's own line, kept so a startup notice can be added to it
    // instead of replacing it.
    QString m_statusBase;

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

    // Every registered module, for announceSelectedVessel(). Non-owning: the
    // registry owns them and outlives this window.
    QList<IModule*> m_modules;

    AlertBanner* m_alertBanner = nullptr;

    // Which sidebar row belongs to which module, so a badge can be rewritten
    // and a drill-down can find the right page later (alerts-spec.md §6).
    QHash<IModule*, int> m_moduleSidebarRows;
};
