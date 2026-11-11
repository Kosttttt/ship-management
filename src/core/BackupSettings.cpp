#include "core/BackupSettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace {

const QString kEnabledKey      = QStringLiteral("backup/automaticEnabled");
const QString kFolderKey       = QStringLiteral("backup/automaticFolder");
const QString kManualFolderKey = QStringLiteral("backup/lastManualFolder");

// Set only by tests. Process-wide, like the two overrides already in the
// project, and for the same reason: there is no other way to keep a test out
// of the real per-user settings.
QString g_settingsFileOverride;

std::unique_ptr<QSettings> openSettings()
{
    if (!g_settingsFileOverride.isEmpty()) {
        return std::make_unique<QSettings>(g_settingsFileOverride, QSettings::IniFormat);
    }
    // Lands under the organisation and application names main() sets.
    return std::make_unique<QSettings>();
}

QString readFolder(const QString& key)
{
    const std::unique_ptr<QSettings> settings = openSettings();
    return settings->value(key).toString();
}

void writeFolder(const QString& key, const QString& folder)
{
    const std::unique_ptr<QSettings> settings = openSettings();
    if (folder.isEmpty()) {
        settings->remove(key);
        return;
    }
    settings->setValue(key, QDir::cleanPath(folder));
}

} // namespace

void BackupSettings::setSettingsFileOverride(const QString& filePath)
{
    g_settingsFileOverride = filePath;
}

bool BackupSettings::automaticEnabled()
{
    const std::unique_ptr<QSettings> settings = openSettings();
    return settings->value(kEnabledKey, false).toBool();
}

void BackupSettings::setAutomaticEnabled(bool on)
{
    const std::unique_ptr<QSettings> settings = openSettings();
    settings->setValue(kEnabledKey, on);
}

QString BackupSettings::automaticFolder()
{
    return readFolder(kFolderKey);
}

void BackupSettings::setAutomaticFolder(const QString& folder)
{
    writeFolder(kFolderKey, folder);
}

QString BackupSettings::lastManualFolder()
{
    const QString remembered = readFolder(kManualFolderKey);
    if (!remembered.isEmpty()) {
        return remembered;
    }

    // Nothing remembered yet: the folder the daily backup already uses is a
    // better guess than Documents, because it is where this computer's
    // backups live. Falls through to Documents when that is unset too.
    const QString automatic = automaticFolder();
    if (!automatic.isEmpty()) {
        return automatic;
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void BackupSettings::setLastManualFolder(const QString& folder)
{
    writeFolder(kManualFolderKey, folder);
}
