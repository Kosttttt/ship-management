#pragma once

#include <QString>
#include <QWidget>

class AppSettingRepository;
class AttachmentFolderSettings;
class DatabaseBackupSettings;
class DocumentsFolderSettings;
class InstallationContext;
class QSqlDatabase;
class QLabel;
class QPushButton;
class QSpinBox;
class VesselRepository;

// The Settings screen (settings-app-setting-spec §6).
//
// A form with no dialog wrapper: it edits the one settings row directly, the
// same way VesselDetailPage edits the one vessel in VESSEL mode. Core-owned
// UI, so it sits alongside VesselListWidget rather than under any module.
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    // Takes the vessel repository as well as the settings row, because the
    // per-vessel attachment folders (certificate-attachment-addendum-spec §2)
    // are a setting too — just one stored on `vessel` rather than in
    // `app_setting`, since the folder belongs to the vessel and not to this
    // installation's preferences.
    // connection, installation and dataFolder are here for the database
    // backup section (database-backup-spec §3). The folder is passed in
    // rather than looked up, so only main() names the real one and a test can
    // point this at a QTemporaryDir (§6).
    SettingsPage(AppSettingRepository&      settings,
                 VesselRepository&          vessels,
                 QSqlDatabase&              connection,
                 const InstallationContext& installation,
                 const QString&             dataFolder,
                 QWidget*                   parent = nullptr);

    // Re-reads the stored row into the form, and the fleet's folders with it.
    void reload();

private:
    void save();

    // The three values must stay strictly increasing. The Save button follows
    // this, so the invalid combination is never something a user can submit —
    // the repository's own check stays as the backstop.
    void updateValidity();
    bool isValid() const;

    AppSettingRepository& m_settings;

    QSpinBox* m_criticalSpin     = nullptr;
    QSpinBox* m_expiringSoonSpin = nullptr;
    QSpinBox* m_dueSoonSpin      = nullptr;

    QLabel*      m_hintLabel   = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QPushButton* m_saveButton  = nullptr;

    // Saves itself the moment a folder is chosen, rather than waiting for the
    // Save button: choosing a folder moves files, which is not something to
    // leave sitting in a form as an unsaved change.
    AttachmentFolderSettings* m_attachmentFolders = nullptr;

    // Its own widget for the same reason, and because nothing about a backup
    // belongs to the Save button either (database-backup-spec §3).
    DatabaseBackupSettings*   m_databaseBackup    = nullptr;

    // Where the Documents module files its uploads
    // (document-management-spec.md §4.5). Its own widget for the same reason
    // the two above are.
    DocumentsFolderSettings*  m_documentsFolder   = nullptr;

    QString m_settingId;
};
