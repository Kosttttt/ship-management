#include "core/DocumentsFolder.h"

#include <QDir>
#include <QSettings>

#include <memory>

namespace {

const QString kFolderKey = QStringLiteral("documents/folder");

// Set only by tests. Process-wide, like the three overrides already in this
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

} // namespace

void DocumentsFolder::setSettingsFileOverride(const QString& filePath)
{
    g_settingsFileOverride = filePath;
}

QString DocumentsFolder::path()
{
    const std::unique_ptr<QSettings> settings = openSettings();
    return settings->value(kFolderKey).toString();
}

void DocumentsFolder::setPath(const QString& absolutePath)
{
    const std::unique_ptr<QSettings> settings = openSettings();
    if (absolutePath.isEmpty()) {
        settings->remove(kFolderKey);
        return;
    }
    settings->setValue(kFolderKey, QDir::cleanPath(absolutePath));
}
