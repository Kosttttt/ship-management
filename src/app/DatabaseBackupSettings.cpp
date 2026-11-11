#include "app/DatabaseBackupSettings.h"

#include "app/RestoreConfirmationDialog.h"
#include "core/AutomaticBackup.h"
#include "core/BackupFileInspector.h"
#include "core/BackupSettings.h"
#include "core/DatabaseBackup.h"
#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "core/RestoreSwap.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSqlDatabase>
#include <QVBoxLayout>

namespace {

const QString kDatabaseFilter = QStringLiteral("Database files (*.db);;All files (*)");

} // namespace

DatabaseBackupSettings::DatabaseBackupSettings(QSqlDatabase&              connection,
                                               const InstallationContext& installation,
                                               const QString&             dataFolder,
                                               QWidget*                   parent)
    : QWidget(parent)
    , m_connection(connection)
    , m_nodeId(installation.nodeId())
    , m_dataFolder(dataFolder)
{
    auto* group = new QGroupBox(tr("Database backup"), this);
    group->setObjectName(QStringLiteral("databaseBackupGroup"));

    auto* explanation =
        new QLabel(tr("A backup is a copy of the database only. Certificate documents stay in "
                      "each vessel's own attachment folder and are copied separately."),
                   group);
    explanation->setWordWrap(true);
    explanation->setEnabled(false); // renders as secondary text

    auto* backUpButton = new QPushButton(tr("&Back up now…"), group);
    backUpButton->setObjectName(QStringLiteral("backUpNowButton"));

    auto* restoreButton = new QPushButton(tr("&Restore from backup…"), group);
    restoreButton->setObjectName(QStringLiteral("restoreBackupButton"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(backUpButton);
    buttonRow->addWidget(restoreButton);
    buttonRow->addStretch();

    m_statusLabel = new QLabel(QString(), group);
    m_statusLabel->setObjectName(QStringLiteral("backupStatusLabel"));
    m_statusLabel->setWordWrap(true);

    m_automaticCheck = new QCheckBox(tr("Back up automatically once a day"), group);
    m_automaticCheck->setObjectName(QStringLiteral("automaticBackupCheck"));

    m_folderLabel = new QLabel(QString(), group);
    m_folderLabel->setObjectName(QStringLiteral("automaticBackupFolderLabel"));
    m_folderLabel->setWordWrap(true);

    auto* chooseButton = new QPushButton(tr("&Choose…"), group);
    chooseButton->setObjectName(QStringLiteral("chooseBackupFolderButton"));

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->addWidget(m_folderLabel, 1);
    folderRow->addWidget(chooseButton, 0);

    m_lastBackupLabel = new QLabel(QString(), group);
    m_lastBackupLabel->setObjectName(QStringLiteral("lastAutomaticBackupLabel"));

    auto* form = new QFormLayout;
    form->addRow(tr("Folder:"), folderRow);

    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(explanation);
    groupLayout->addLayout(buttonRow);
    groupLayout->addWidget(m_statusLabel);
    groupLayout->addSpacing(8);
    groupLayout->addWidget(m_automaticCheck);
    groupLayout->addLayout(form);
    groupLayout->addWidget(m_lastBackupLabel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group);

    // The stored state is applied before anything is connected, so building
    // this widget only ever *reads* the settings. Connecting first would have
    // setChecked() write the value straight back — harmless here, but the
    // sort of thing that makes a test touch the real settings file.
    m_automaticCheck->setChecked(BackupSettings::automaticEnabled());
    updateAutomaticLines();

    connect(backUpButton, &QPushButton::clicked, this, &DatabaseBackupSettings::backUpNow);
    connect(restoreButton, &QPushButton::clicked, this,
            &DatabaseBackupSettings::restoreFromBackup);
    connect(chooseButton, &QPushButton::clicked, this,
            &DatabaseBackupSettings::chooseAutomaticFolder);
    connect(m_automaticCheck, &QCheckBox::toggled, this, [this](bool on) {
        BackupSettings::setAutomaticEnabled(on);
        updateAutomaticLines();
    });
}

void DatabaseBackupSettings::reload()
{
    updateAutomaticLines();
}

void DatabaseBackupSettings::updateAutomaticLines()
{
    const QString folder = BackupSettings::automaticFolder();

    m_folderLabel->setText(folder.isEmpty() ? tr("No folder chosen yet.")
                                            : QDir::toNativeSeparators(folder));
    m_folderLabel->setEnabled(!folder.isEmpty());

    if (folder.isEmpty()) {
        m_lastBackupLabel->setText(tr("Last automatic backup: None yet"));
        return;
    }

    // Read from the folder rather than from a stored date, the same way §4
    // decides whether today's backup is due: a folder that was emptied or
    // swapped tells the truth about itself.
    const QDateTime last = AutomaticBackup::lastBackupTime(folder, m_nodeId);
    m_lastBackupLabel->setText(
        last.isValid()
            ? tr("Last automatic backup: %1").arg(last.toString(QStringLiteral("dd MMM yyyy, HH:mm")))
            : tr("Last automatic backup: None yet"));
}

void DatabaseBackupSettings::backUpNow()
{
    const QString suggested =
        DatabaseBackup::manualFileName(m_nodeId, QDateTime::currentDateTime());
    const QString startIn = QDir(BackupSettings::lastManualFolder()).filePath(suggested);

    const QString chosen =
        QFileDialog::getSaveFileName(this, tr("Save a backup"), startIn, kDatabaseFilter);
    if (chosen.isEmpty()) {
        return; // cancelled
    }

    QString error;
    if (!DatabaseBackup::writeTo(m_connection, chosen, &error)) {
        m_statusLabel->setText(tr("The backup was not saved."));
        QMessageBox::warning(this, tr("Could not save the backup"), error);
        return;
    }

    BackupSettings::setLastManualFolder(QFileInfo(chosen).absolutePath());

    // The counts come from the same place the restore dialog reads them, so
    // the number shown here and the number shown there cannot disagree.
    BackupSummary summary;
    QString       summaryError;
    const QString saved = QDir::toNativeSeparators(chosen);
    if (BackupFileInspector::summarise(m_connection, &summary, &summaryError)) {
        m_statusLabel->setText(tr("Backup saved: %1 (%2 vessels, %3 certificates).")
                                   .arg(saved)
                                   .arg(summary.vesselCount)
                                   .arg(summary.certificateCount));
    } else {
        // The backup itself is written and checked; only the sentence about
        // it is short of two numbers.
        m_statusLabel->setText(tr("Backup saved: %1").arg(saved));
    }
}

void DatabaseBackupSettings::restoreFromBackup()
{
    const QString chosen = QFileDialog::getOpenFileName(
        this, tr("Choose a backup to restore"), BackupSettings::lastManualFolder(),
        kDatabaseFilter);
    if (chosen.isEmpty()) {
        return; // cancelled
    }

    // §5.1: every check happens on a read-only connection of its own before
    // anything at all is written.
    MigrationRunner runner(m_connection, MigrationRunner::compiledInPath());
    int             highestKnown = 0;
    if (!runner.highestAvailableVersion(&highestKnown)) {
        QMessageBox::warning(this, tr("Could not check the backup"), runner.errorString());
        return;
    }

    BackupSummary backup;
    QString       error;
    if (!BackupFileInspector::inspect(chosen, highestKnown, &backup, &error)) {
        QMessageBox::warning(this, tr("This backup cannot be restored"), error);
        return;
    }

    BackupSummary current;
    if (!BackupFileInspector::summarise(m_connection, &current, &error)) {
        QMessageBox::warning(this, tr("Could not read this computer's database"), error);
        return;
    }

    RestoreConfirmationDialog confirmation(current, backup, chosen, this);
    if (confirmation.exec() != QDialog::Accepted) {
        return;
    }

    // §5.3 step 1. Still nothing replaced: the backup is copied in beside the
    // live database under a pending name, and the swap happens at the next
    // start, before anything opens it.
    if (!RestoreSwap::stage(m_dataFolder, chosen, &error)) {
        QMessageBox::warning(this, tr("Could not prepare the restore"), error);
        return;
    }

    restartToFinishRestore();
}

void DatabaseBackupSettings::restartToFinishRestore()
{
    const QString program = QCoreApplication::applicationFilePath();

    if (QProcess::startDetached(program, QStringList())) {
        qInfo() << "Restarting to finish a restore.";
        QCoreApplication::quit();
        return;
    }

    // The pending file is in place either way, so the restore is not lost —
    // it simply waits for the next start.
    QMessageBox::information(this, tr("Restart to finish"),
                             tr("The backup is ready to be restored, but the program could not "
                                "start itself again.\n\nStart the program again to finish "
                                "restoring."));
    QCoreApplication::quit();
}

void DatabaseBackupSettings::chooseAutomaticFolder()
{
    const QString current = BackupSettings::automaticFolder();
    const QString chosen  = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder for automatic backups"),
        current.isEmpty() ? BackupSettings::lastManualFolder() : current,
        QFileDialog::ShowDirsOnly);
    if (chosen.isEmpty()) {
        return; // cancelled: leave the stored folder exactly as it was
    }

    BackupSettings::setAutomaticFolder(chosen);
    updateAutomaticLines();

    if (!m_automaticCheck->isChecked()) {
        // Choosing a folder is how somebody says they want this on, so the
        // checkbox follows rather than making them find it. Its own signal
        // stores the value.
        m_automaticCheck->setChecked(true);
    }
}
