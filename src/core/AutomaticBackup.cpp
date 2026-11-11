#include "core/AutomaticBackup.h"

#include "core/DatabaseBackup.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace {

// Every entry in the folder that is one of this node's automatic backups,
// paired with the date in its name. Nothing else in the folder is even
// considered, which is what keeps pruning away from manual backups and other
// computers' files.
QList<QPair<QDate, QString>> automaticBackupsIn(const QString& folder, const QString& nodeId)
{
    QList<QPair<QDate, QString>> found;

    const QDir directory(folder);
    if (folder.isEmpty() || !directory.exists()) {
        return found;
    }

    const QStringList entries = directory.entryList(QDir::Files);
    for (const QString& name : entries) {
        const QDate date = DatabaseBackup::dateOfAutomaticFileName(name, nodeId);
        if (date.isValid()) {
            found.append({date, name});
        }
    }

    // Newest first. Two files cannot share a date — the name carries it — so
    // the order is total.
    std::sort(found.begin(), found.end(),
              [](const QPair<QDate, QString>& a, const QPair<QDate, QString>& b) {
                  return a.first > b.first;
              });
    return found;
}

} // namespace

QList<QDate> AutomaticBackup::existingDates(const QString& folder, const QString& nodeId)
{
    QList<QDate> dates;
    for (const QPair<QDate, QString>& backup : automaticBackupsIn(folder, nodeId)) {
        dates.append(backup.first);
    }
    return dates;
}

bool AutomaticBackup::isDueToday(const QString& folder, const QString& nodeId, QDate today)
{
    for (const QDate& date : existingDates(folder, nodeId)) {
        if (date == today) {
            return false;
        }
    }
    return true;
}

QDateTime AutomaticBackup::lastBackupTime(const QString& folder, const QString& nodeId)
{
    const QList<QPair<QDate, QString>> backups = automaticBackupsIn(folder, nodeId);
    if (backups.isEmpty()) {
        return QDateTime();
    }
    return QFileInfo(QDir(folder).filePath(backups.first().second)).lastModified();
}

int AutomaticBackup::prune(const QString& folder, const QString& nodeId, int keep,
                           QStringList* removed)
{
    if (removed != nullptr) {
        removed->clear();
    }
    if (keep < 0) {
        return 0;
    }

    const QList<QPair<QDate, QString>> backups = automaticBackupsIn(folder, nodeId);
    int                                deleted = 0;

    // Newest first, so everything from index `keep` on is older than the ones
    // being kept.
    for (int index = keep; index < backups.size(); ++index) {
        const QString path = QDir(folder).filePath(backups.at(index).second);
        if (!QFile::remove(path)) {
            // A file that will not delete — open in a sync client, say — is
            // not worth failing a backup over. The next run tries again.
            qWarning() << "Could not remove an old automatic backup:" << path;
            continue;
        }
        ++deleted;
        if (removed != nullptr) {
            removed->append(backups.at(index).second);
        }
    }
    return deleted;
}

AutomaticBackup::Outcome AutomaticBackup::runIfDue(QSqlDatabase& connection, const QString& folder,
                                                   const QString& nodeId, QDate today,
                                                   QString* savedPath, QString* errorString)
{
    savedPath->clear();
    errorString->clear();

    if (folder.isEmpty()) {
        *errorString = QStringLiteral("No folder has been chosen for automatic backups.");
        return Outcome::NoFolder;
    }
    if (!QDir(folder).exists()) {
        // The Google Drive folder that is not running, the USB drive that is
        // not plugged in. Reported, never fatal (§4).
        *errorString = QStringLiteral(
                           "The automatic backup folder is not there:\n%1\n\nIf it is on a cloud "
                           "drive or a removable disk, check that it is connected.")
                           .arg(QDir::toNativeSeparators(folder));
        return Outcome::NoFolder;
    }

    if (!isDueToday(folder, nodeId, today)) {
        return Outcome::AlreadyDoneToday;
    }

    const QString target =
        QDir(folder).filePath(DatabaseBackup::automaticFileName(nodeId, today));

    if (!DatabaseBackup::writeTo(connection, target, errorString)) {
        return Outcome::Failed;
    }

    *savedPath = target;

    // Only after a successful write, so a failed backup never thins out the
    // copies that are still good.
    prune(folder, nodeId, kKeepNewest);

    return Outcome::Saved;
}
