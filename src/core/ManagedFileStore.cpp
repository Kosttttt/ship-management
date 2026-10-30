#include "core/ManagedFileStore.h"

#include "core/Database.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// Process-wide, and empty in the application itself: only a test fixture or
// CLAUDE.md §10.1's throwaway data directory ever sets it.
QString g_rootOverride;

void setError(QString* out, const QString& text)
{
    if (out != nullptr) {
        *out = text;
    }
}

QString nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

// "report.pdf" with index 3 becomes "report_3.pdf", not "report.pdf_3": the
// extension has to stay last or the operating system stops recognising the
// file. A folder has no extension, so it simply gains the suffix.
QString nameWithIndex(const QString& desiredName, int index)
{
    if (index <= 1) {
        return desiredName;
    }

    const QFileInfo info(desiredName);
    const QString   suffix = info.suffix();
    if (suffix.isEmpty()) {
        return QStringLiteral("%1_%2").arg(desiredName).arg(index);
    }
    return QStringLiteral("%1_%2.%3").arg(info.completeBaseName()).arg(index).arg(suffix);
}

// Deletes a file or a whole directory, used only to clean up after a copy that
// failed half way — so a failure never leaves a partial attachment behind.
void removeAnything(const QString& absolutePath)
{
    const QFileInfo info(absolutePath);
    if (!info.exists()) {
        return;
    }
    if (info.isDir()) {
        QDir(absolutePath).removeRecursively();
    } else {
        QFile::remove(absolutePath);
    }
}

// One function for both kinds of source, which is what lets every caller stay
// free of "is it a file or a folder" branching.
bool copyRecursively(const QString& source, const QString& destination, QString* errorString)
{
    const QFileInfo info(source);

    if (!info.isDir()) {
        if (!QFile::copy(source, destination)) {
            setError(errorString,
                     QStringLiteral("Could not copy the file:\n%1\n\nto\n%2")
                         .arg(nativePath(source), nativePath(destination)));
            return false;
        }
        return true;
    }

    if (!QDir().mkpath(destination)) {
        setError(errorString,
                 QStringLiteral("Could not create the folder:\n%1").arg(nativePath(destination)));
        return false;
    }

    // NoSymLinks deliberately: a symbolic link pointing back at an ancestor
    // would otherwise recurse until the disk filled.
    const QDir          sourceDir(source);
    const QFileInfoList entries =
        sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System
                                | QDir::NoDotAndDotDot | QDir::NoSymLinks);

    for (const QFileInfo& entry : entries) {
        if (!copyRecursively(entry.absoluteFilePath(),
                             QDir(destination).filePath(entry.fileName()), errorString)) {
            return false;
        }
    }
    return true;
}

} // namespace

QString ManagedFileStore::rootPath()
{
    if (!g_rootOverride.isEmpty()) {
        return g_rootOverride;
    }

    // The one resolution of the managed data folder in this application. Asked
    // of Database rather than of QStandardPaths so that the folder the files go
    // into is, by construction, the folder the database file is already in.
    return QFileInfo(Database::defaultFilePath()).absolutePath();
}

void ManagedFileStore::setRootPathOverride(const QString& absolutePath)
{
    g_rootOverride =
        absolutePath.isEmpty() ? QString() : QFileInfo(absolutePath).absoluteFilePath();
}

bool ManagedFileStore::copyInto(const QString& sourcePath,
                                const QString& destinationRelativeDir,
                                const QString& desiredName,
                                QString*       relativePathOut,
                                QString*       errorString)
{
    setError(errorString, QString());

    if (desiredName.isEmpty()) {
        setError(errorString, QStringLiteral("No name was given for the copied file."));
        return false;
    }

    const QFileInfo sourceInfo(sourcePath);
    if (sourcePath.isEmpty() || !sourceInfo.exists()) {
        setError(errorString, QStringLiteral("This file or folder could not be found:\n%1")
                                  .arg(nativePath(sourcePath)));
        return false;
    }

    const QString root = rootPath();
    if (root.isEmpty()) {
        setError(errorString, QStringLiteral("The managed data folder could not be located."));
        return false;
    }

    const QDir    rootDir(root);
    const QString absoluteDir =
        destinationRelativeDir.isEmpty() ? root : rootDir.filePath(destinationRelativeDir);

    if (!QDir().mkpath(absoluteDir)) {
        setError(errorString,
                 QStringLiteral("Could not create the folder:\n%1").arg(nativePath(absoluteDir)));
        return false;
    }

    // The numeric suffix is added only on an actual collision, so the ordinary
    // case keeps the clean generated name.
    QString absoluteTarget;
    for (int index = 1;; ++index) {
        if (index > 999) {
            setError(errorString,
                     QStringLiteral("Too many files named %1 are already stored here.")
                         .arg(desiredName));
            return false;
        }
        absoluteTarget = QDir(absoluteDir).filePath(nameWithIndex(desiredName, index));
        if (!QFileInfo::exists(absoluteTarget)) {
            break;
        }
    }

    if (!copyRecursively(sourceInfo.absoluteFilePath(), absoluteTarget, errorString)) {
        // Half a folder is worse than none: the caller is told it failed, so
        // what it would otherwise point at must not exist.
        removeAnything(absoluteTarget);
        return false;
    }

    if (relativePathOut != nullptr) {
        // Relative, with forward slashes, exactly as it will be stored
        // (CLAUDE.md §6.9).
        *relativePathOut = rootDir.relativeFilePath(absoluteTarget);
    }
    return true;
}

bool ManagedFileStore::moveToArchive(const QString& relativePath,
                                     const QString& archiveRelativeDir,
                                     QString*       errorString)
{
    setError(errorString, QString());

    if (relativePath.isEmpty()) {
        setError(errorString, QStringLiteral("There is nothing to archive."));
        return false;
    }

    const QString root = rootPath();
    if (root.isEmpty()) {
        setError(errorString, QStringLiteral("The managed data folder could not be located."));
        return false;
    }

    const QDir    rootDir(root);
    const QString absoluteSource = rootDir.filePath(relativePath);

    if (!QFileInfo::exists(absoluteSource)) {
        setError(errorString, QStringLiteral("The stored file or folder is no longer there:\n%1")
                                  .arg(nativePath(absoluteSource)));
        return false;
    }

    const QString archiveDir = rootDir.filePath(archiveRelativeDir);
    if (!QDir().mkpath(archiveDir)) {
        setError(errorString, QStringLiteral("Could not create the archive folder:\n%1")
                                  .arg(nativePath(archiveDir)));
        return false;
    }

    // Compact ISO-8601 UTC (CLAUDE.md §6.2). The usual form contains colons,
    // which are not legal in a Windows filename. The two separators are
    // concatenated rather than escaped inside one format string, so there is
    // no question of Qt reading a "T" or a "Z" as a field of its own.
    const QDateTime now   = QDateTime::currentDateTimeUtc();
    const QString   stamp = now.date().toString(QStringLiteral("yyyyMMdd"))
                          + QStringLiteral("T") + now.time().toString(QStringLiteral("hhmmss"))
                          + QStringLiteral("Z");
    const QString archivedName =
        QStringLiteral("%1__archived-%2").arg(QFileInfo(absoluteSource).fileName(), stamp);

    QString absoluteTarget = QDir(archiveDir).filePath(archivedName);
    for (int index = 2; QFileInfo::exists(absoluteTarget); ++index) {
        // Two archives within the same second: rare, but the timestamp alone
        // would then not be unique.
        if (index > 999) {
            setError(errorString, QStringLiteral("Could not find a free name in the archive."));
            return false;
        }
        absoluteTarget =
            QDir(archiveDir).filePath(QStringLiteral("%1_%2").arg(archivedName).arg(index));
    }

    // Rename rather than copy-then-delete: it moves a file and a whole folder
    // alike, and both paths are inside the managed folder by construction, so
    // it stays within one volume.
    if (!QDir().rename(absoluteSource, absoluteTarget)) {
        setError(errorString, QStringLiteral("Could not move this into the archive:\n%1")
                                  .arg(nativePath(absoluteSource)));
        return false;
    }
    return true;
}

QString ManagedFileStore::resolveAbsolute(const QString& relativePath)
{
    if (relativePath.isEmpty()) {
        return QString();
    }
    return QDir(rootPath()).filePath(relativePath);
}
