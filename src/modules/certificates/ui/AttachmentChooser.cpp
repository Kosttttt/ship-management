#include "modules/certificates/ui/AttachmentChooser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace {

// A user-interface preference, not fleet data, so QSettings rather than
// app_setting or anywhere else in the database — the same call
// TableHeaderSettings makes, for the same reason. Which folder this machine's
// documents happen to sit in means nothing on the other side of a
// ship-to-shore sync.
const QString kLastFolderKey = QStringLiteral("ui/attachmentChooser/lastFolder");

// Set only by tests. Process-wide, like TableHeaderSettings' own override, and
// for the same reason: there is no other way to keep a test out of the real
// per-user settings.
QString g_settingsFileOverride;

std::unique_ptr<QSettings> openSettings()
{
    if (!g_settingsFileOverride.isEmpty()) {
        return std::make_unique<QSettings>(g_settingsFileOverride, QSettings::IniFormat);
    }
    // Lands under the organisation and application names main() sets.
    return std::make_unique<QSettings>();
}

QString documentsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

// Whether candidate is root itself or sits somewhere beneath it.
//
// Compared case-insensitively on every platform, which is deliberately the
// stricter reading rather than the accurate one. On Windows it is simply
// correct. On Linux, where two folders may differ only in case, it can say
// "inside" about a folder that is not — and the cost of that is opening at
// Documents instead, which is no worse than the first-run behaviour. The
// mistake the other way round would open the dialog inside the managed folder,
// where a source must never be picked from (certificate-attachment-spec §8,
// housekeeping-round-spec §5).
bool isAtOrInside(const QString& candidate, const QString& root)
{
    if (candidate.isEmpty() || root.isEmpty()) {
        return false;
    }

    const QString cleanCandidate = QDir::cleanPath(QDir(candidate).absolutePath());
    const QString cleanRoot      = QDir::cleanPath(QDir(root).absolutePath());

    if (cleanCandidate.compare(cleanRoot, Qt::CaseInsensitive) == 0) {
        return true;
    }
    // The trailing separator matters: without it "/data/ships-old" would count
    // as being inside "/data/ships".
    return cleanCandidate.startsWith(cleanRoot + QLatin1Char('/'), Qt::CaseInsensitive);
}

void remember(const QString& folderPath)
{
    if (folderPath.isEmpty()) {
        return;
    }
    const std::unique_ptr<QSettings> settings = openSettings();
    settings->setValue(kLastFolderKey, QDir::cleanPath(folderPath));
}

} // namespace

void AttachmentChooser::setSettingsFileOverride(const QString& filePath)
{
    g_settingsFileOverride = filePath;
}

QString AttachmentChooser::startingFolder(const QString& vesselAttachmentRoot)
{
    const std::unique_ptr<QSettings> settings = openSettings();
    const QString remembered = settings->value(kLastFolderKey).toString();

    if (remembered.isEmpty()) {
        return documentsFolder(); // every user's first time
    }

    // A folder that has since been renamed, deleted or unplugged. Qt would
    // quietly fall back to the working directory, which is wherever the
    // application happens to have been started from.
    if (!QFileInfo(remembered).isDir()) {
        return documentsFolder();
    }

    // The one folder a source must never be picked from: copying a file onto
    // itself is the sort of thing that only ever goes wrong. The remembered
    // folder can land here the moment a vessel's attachment folder is changed
    // to somewhere a document was once picked from.
    if (isAtOrInside(remembered, vesselAttachmentRoot)) {
        return documentsFolder();
    }

    return remembered;
}

void AttachmentChooser::rememberChosenFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return;
    }
    // The folder holding it: reopening at the file itself would single that
    // one document out, which is not what "where I was last working" means.
    remember(QFileInfo(filePath).absolutePath());
}

void AttachmentChooser::rememberChosenFolder(const QString& folderPath)
{
    if (folderPath.isEmpty()) {
        return;
    }
    // The parent, so the folder just chosen is visible among its neighbours
    // next time rather than being the one folder shown from the inside.
    //
    // A root directory has no parent, and QDir::cdUp() leaves the path alone
    // and returns false there; remembering the root itself is the only
    // sensible answer in that case, so the result is used either way.
    QDir parent(folderPath);
    parent.cdUp();
    remember(parent.absolutePath());
}

QString AttachmentChooser::chooseFile(QWidget* parent, const QString& vesselAttachmentRoot)
{
    const QString chosen = QFileDialog::getOpenFileName(
        parent, QCoreApplication::translate("AttachmentChooser", "Choose the document"),
        startingFolder(vesselAttachmentRoot));

    // Only on a real pick. Cancel returns empty and must leave the remembered
    // folder exactly as it was.
    rememberChosenFile(chosen);
    return chosen;
}

QString AttachmentChooser::chooseFolder(QWidget* parent, const QString& vesselAttachmentRoot)
{
    // ShowDirsOnly is the default, spelled out because the dialog is otherwise
    // easy to mistake for the file one.
    const QString chosen = QFileDialog::getExistingDirectory(
        parent, QCoreApplication::translate("AttachmentChooser", "Choose the folder of documents"),
        startingFolder(vesselAttachmentRoot), QFileDialog::ShowDirsOnly);

    rememberChosenFolder(chosen);
    return chosen;
}
