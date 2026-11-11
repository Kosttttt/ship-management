#include "core/DatabaseBackup.h"

#include "core/BackupFileInspector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

const QString kPrefix        = QStringLiteral("ship-management-");
const QString kAutoMarker    = QStringLiteral("auto-");
const QString kSuffix        = QStringLiteral(".db");
const QString kTempExtension = QStringLiteral(".tmp");

// A node id is 'OFFICE' or 'VESSEL-<IMO>', so this changes nothing in
// practice. It is here so that a node id from a future naming scheme cannot
// put a path separator or a colon into a filename.
QString forFileName(const QString& nodeId)
{
    QString safe;
    safe.reserve(nodeId.size());
    for (const QChar character : nodeId) {
        safe.append(character.isLetterOrNumber() || character == QLatin1Char('-')
                            || character == QLatin1Char('_')
                        ? character
                        : QLatin1Char('_'));
    }
    return safe;
}

// A path inside a SQL string literal. Doubling the quote is SQLite's own
// escape, and the only character that needs one here.
QString asSqlLiteral(const QString& path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

QString DatabaseBackup::manualFileName(const QString& nodeId, const QDateTime& when)
{
    return kPrefix + forFileName(nodeId) + QLatin1Char('-')
           + when.toString(QStringLiteral("yyyy-MM-dd_HHmm")) + kSuffix;
}

QString DatabaseBackup::automaticFileName(const QString& nodeId, const QDate& when)
{
    return kPrefix + kAutoMarker + forFileName(nodeId) + QLatin1Char('-')
           + when.toString(QStringLiteral("yyyy-MM-dd")) + kSuffix;
}

bool DatabaseBackup::isAutomaticFileName(const QString& fileName, const QString& nodeId)
{
    return dateOfAutomaticFileName(fileName, nodeId).isValid();
}

QDate DatabaseBackup::dateOfAutomaticFileName(const QString& fileName, const QString& nodeId)
{
    const QString expectedStart = kPrefix + kAutoMarker + forFileName(nodeId) + QLatin1Char('-');

    // Case-sensitively on the node, deliberately: two nodes whose ids differ
    // only in case would be two different installations, and pruning must
    // never reach another computer's files (§4).
    if (!fileName.startsWith(expectedStart) || !fileName.endsWith(kSuffix)) {
        return QDate();
    }

    const int dateLength = fileName.size() - expectedStart.size() - kSuffix.size();
    if (dateLength <= 0) {
        return QDate();
    }

    // An invalid QDate from a name that merely looks right — a longer node id
    // beginning with this one, say — is the answer: not one of ours.
    return QDate::fromString(fileName.mid(expectedStart.size(), dateLength),
                             QStringLiteral("yyyy-MM-dd"));
}

bool DatabaseBackup::writeTo(QSqlDatabase& connection, const QString& targetFilePath,
                             QString* errorString)
{
    errorString->clear();

    if (!connection.isValid() || !connection.isOpen()) {
        *errorString = QStringLiteral("The database is not open, so nothing could be backed up.");
        return false;
    }

    const QFileInfo target(targetFilePath);
    const QString   folder = target.absolutePath();
    if (!QDir(folder).exists()) {
        *errorString = QStringLiteral(
                           "The backup folder is not there:\n%1\n\nIf it is on a cloud drive or a "
                           "removable disk, check that it is connected.")
                           .arg(QDir::toNativeSeparators(folder));
        return false;
    }

    const QString temporaryPath = targetFilePath + kTempExtension;

    // VACUUM INTO refuses to write to a file that already exists, so a
    // leftover temporary from an interrupted run has to go first. It is our
    // own file and nothing else ever has this name.
    if (QFile::exists(temporaryPath) && !QFile::remove(temporaryPath)) {
        *errorString = QStringLiteral("An unfinished backup is in the way and could not be "
                                      "removed:\n%1")
                           .arg(QDir::toNativeSeparators(temporaryPath));
        return false;
    }

    QSqlQuery vacuum(connection);
    if (!vacuum.exec(QStringLiteral("VACUUM INTO %1").arg(asSqlLiteral(temporaryPath)))) {
        *errorString = QStringLiteral("The backup could not be written:\n%1")
                           .arg(vacuum.lastError().text());
        QFile::remove(temporaryPath); // may not exist; a failed remove changes nothing
        return false;
    }

    // Checked before it is given its real name, so a damaged backup never
    // exists under a name that looks like a good one.
    QString integrityError;
    if (!BackupFileInspector::passesIntegrityCheck(temporaryPath, &integrityError)) {
        QFile::remove(temporaryPath);
        *errorString = QStringLiteral("The backup was written but failed its own check, so it has "
                                      "been removed:\n%1")
                           .arg(integrityError);
        return false;
    }

    // Only now that a complete, checked file exists. The save dialog is where
    // the user agreed to overwrite; reaching here means they did.
    if (QFile::exists(targetFilePath) && !QFile::remove(targetFilePath)) {
        QFile::remove(temporaryPath);
        *errorString = QStringLiteral("The existing backup could not be replaced:\n%1")
                           .arg(QDir::toNativeSeparators(targetFilePath));
        return false;
    }

    if (!QFile::rename(temporaryPath, targetFilePath)) {
        QFile::remove(temporaryPath);
        *errorString = QStringLiteral("The backup could not be given its final name:\n%1")
                           .arg(QDir::toNativeSeparators(targetFilePath));
        return false;
    }

    return true;
}
