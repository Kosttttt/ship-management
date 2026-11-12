#pragma once

#include <QString>
#include <QWidget>

class InstallationContext;
class QCheckBox;
class QLabel;
class QSqlDatabase;

// The "Database backup" section of the Settings screen
// (database-backup-spec §3).
//
// Its own widget the way AttachmentFolderSettings is, which keeps
// SettingsPage under the ~300-line guideline and puts everything about
// backups in one place.
//
// Both installation modes get it: a vessel's computer needs backups as much
// as the office does (§3).
class DatabaseBackupSettings : public QWidget
{
    Q_OBJECT

public:
    // `connection` is the program's own open connection — VACUUM INTO reads
    // through it, so the backup comes from the database actually in use
    // rather than from a path guessed at (§1, CLAUDE.md §10.1).
    //
    // `dataFolder` is where the live database sits, needed to put a pending
    // restore beside it. Passed in rather than looked up, so a test can point
    // this at a QTemporaryDir and only main() ever names the real folder (§6).
    DatabaseBackupSettings(QSqlDatabase&              connection,
                           const InstallationContext& installation,
                           const QString&             dataFolder,
                           QWidget*                   parent = nullptr);

    // Re-reads the folder and the "last automatic backup" line. Called by
    // SettingsPage whenever the page is shown.
    void reload();

private:
    void backUpNow();
    void restoreFromBackup();
    void chooseAutomaticFolder();

    // Everything the stored settings drive: the folder shown, whether the
    // Choose… button is useful, and when the last automatic backup ran.
    void updateAutomaticLines();

    QSqlDatabase& m_connection;
    QString       m_nodeId;
    QString       m_dataFolder;

    QCheckBox* m_automaticCheck = nullptr;
    QLabel*    m_folderLabel    = nullptr;
    QLabel*    m_lastBackupLabel = nullptr;
    QLabel*    m_statusLabel     = nullptr;
};
