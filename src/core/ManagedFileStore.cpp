#include "core/ManagedFileStore.h"

#include "core/Database.h"
#include "core/FileTree.h"

#include <QDateTime>
#include <QDebug>
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

// Whether a path is safe to join onto a root. Anything absolute, and anything
// that climbs back out with "..", is not: QDir::filePath() returns an absolute
// argument unchanged, so joining one would silently name somewhere else
// entirely. Only the destructive operations check this, because only they
// cannot be taken back.
bool isContainableRelativePath(const QString& relativePath)
{
    if (QDir::isAbsolutePath(relativePath)) {
        return false;
    }

    const QString cleaned = QDir::cleanPath(relativePath);

    // "." is the root itself, and ".." or anything beginning "../" climbs out
    // of it. Each would name a folder nobody asked to have deleted.
    if (cleaned.isEmpty() || cleaned == QStringLiteral(".")
        || cleaned == QStringLiteral("..")) {
        return false;
    }
    return !cleaned.startsWith(QStringLiteral("../"));
}

// Compact ISO-8601 UTC (CLAUDE.md §6.2). The usual form contains colons,
// which are not legal in a Windows filename. The two separators are
// concatenated rather than escaped inside one format string, so there is no
// question of Qt reading a "T" or a "Z" as a field of its own.
QString archiveStamp()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return now.date().toString(QStringLiteral("yyyyMMdd")) + QStringLiteral("T")
           + now.time().toString(QStringLiteral("hhmmss")) + QStringLiteral("Z");
}

} // namespace

QString ManagedFileStore::rootPath()
{
    if (!g_rootOverride.isEmpty()) {
        return g_rootOverride;
    }

    // The one resolution of the installation's own data folder in this
    // application. Asked of Database rather than of QStandardPaths so that the
    // folder is, by construction, the folder the database file is already in.
    return QFileInfo(Database::defaultFilePath()).absolutePath();
}

void ManagedFileStore::setRootPathOverride(const QString& absolutePath)
{
    g_rootOverride =
        absolutePath.isEmpty() ? QString() : QFileInfo(absolutePath).absoluteFilePath();
}

QString ManagedFileStore::rootPathOverride()
{
    return g_rootOverride;
}

bool ManagedFileStore::copyInto(const QString& root,
                                const QString& sourcePath,
                                const QString& destinationRelativeDir,
                                const QString& desiredName,
                                QString*       relativePathOut,
                                QString*       errorString)
{
    setError(errorString, QString());

    if (root.isEmpty()) {
        setError(errorString, QStringLiteral("No folder was given to copy this into."));
        return false;
    }

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
        absoluteTarget = QDir(absoluteDir).filePath(FileTree::nameWithIndex(desiredName, index));
        if (!QFileInfo::exists(absoluteTarget)) {
            break;
        }
    }

    if (!FileTree::copyRecursively(sourceInfo.absoluteFilePath(), absoluteTarget, errorString)) {
        // Half a folder is worse than none: the caller is told it failed, so
        // what it would otherwise point at must not exist.
        FileTree::removeAnything(absoluteTarget);
        return false;
    }

    if (relativePathOut != nullptr) {
        // Relative, with forward slashes, exactly as it will be stored
        // (CLAUDE.md §6.9).
        *relativePathOut = rootDir.relativeFilePath(absoluteTarget);
    }
    return true;
}

bool ManagedFileStore::moveToArchive(const QString& root,
                                     const QString& relativePath,
                                     const QString& archiveRelativeDir,
                                     QString*       errorString)
{
    setError(errorString, QString());

    if (relativePath.isEmpty()) {
        setError(errorString, QStringLiteral("There is nothing to archive."));
        return false;
    }

    if (root.isEmpty()) {
        setError(errorString, QStringLiteral("No folder was given to archive this within."));
        return false;
    }

    const QDir    rootDir(root);
    const QString absoluteSource = rootDir.filePath(relativePath);

    const QFileInfo sourceInfo(absoluteSource);
    if (!sourceInfo.exists()) {
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

    // §2: before the extension, not after it, so the archived copy is still a
    // .pdf to the operating system. A folder has no extension to protect and
    // keeps the marker at the end.
    const QString archivedName =
        FileTree::nameWithMarker(sourceInfo.fileName(),
                                 QStringLiteral("__archived-") + archiveStamp(),
                                 sourceInfo.isDir());

    // Two archives within the same second: rare, but the timestamp alone would
    // then not be unique. The index goes before the extension too, which is
    // what nameWithIndex() is for.
    QString absoluteTarget = QDir(archiveDir).filePath(archivedName);
    for (int index = 2; QFileInfo::exists(absoluteTarget); ++index) {
        if (index > 999) {
            setError(errorString, QStringLiteral("Could not find a free name in the archive."));
            return false;
        }
        absoluteTarget =
            QDir(archiveDir).filePath(FileTree::nameWithIndex(archivedName, index));
    }

    // Rename rather than copy-then-delete: it moves a file and a whole folder
    // alike, and both paths are inside the same root by construction, so it
    // stays within one volume.
    if (!QDir().rename(absoluteSource, absoluteTarget)) {
        setError(errorString, QStringLiteral("Could not move this into the archive:\n%1")
                                  .arg(nativePath(absoluteSource)));
        return false;
    }
    return true;
}

bool ManagedFileStore::deleteAt(const QString& root,
                                const QString& relativePath,
                                QString*       errorString)
{
    setError(errorString, QString());

    if (root.isEmpty()) {
        setError(errorString, QStringLiteral("No folder was given to delete this from."));
        return false;
    }

    // "Nothing attached" must never become "delete the whole folder".
    if (relativePath.isEmpty()) {
        setError(errorString, QStringLiteral("There is nothing to delete."));
        return false;
    }

    if (!isContainableRelativePath(relativePath)) {
        setError(errorString,
                 QStringLiteral("This stored path is not inside the vessel's folder, so it will "
                                "not be deleted:\n%1")
                     .arg(nativePath(relativePath)));
        return false;
    }

    const QString absolutePath = QDir(root).filePath(relativePath);

    if (!FileTree::removeAnything(absolutePath)) {
        setError(errorString, QStringLiteral("Could not delete:\n%1")
                                  .arg(nativePath(absolutePath)));
        return false;
    }
    return true;
}

QString ManagedFileStore::resolveAbsolute(const QString& root, const QString& relativePath)
{
    if (root.isEmpty() || relativePath.isEmpty()) {
        return QString();
    }
    return QDir(root).filePath(relativePath);
}

bool ManagedFileStore::moveTree(const QString& sourceAbsolutePath,
                                const QString& targetAbsolutePath,
                                QString*       errorString)
{
    setError(errorString, QString());

    if (sourceAbsolutePath.isEmpty() || targetAbsolutePath.isEmpty()) {
        setError(errorString, QStringLiteral("No folder was given to move."));
        return false;
    }

    if (!QFileInfo::exists(sourceAbsolutePath)) {
        setError(errorString, QStringLiteral("There is nothing to move at:\n%1")
                                  .arg(nativePath(sourceAbsolutePath)));
        return false;
    }

    // Refused rather than merged. Merging two trees would quietly mix one
    // vessel's documents into another's, which is the whole thing §2.5's
    // "already in use by another vessel" check exists to prevent.
    if (QFileInfo::exists(targetAbsolutePath)) {
        setError(errorString, QStringLiteral("Something is already here:\n%1")
                                  .arg(nativePath(targetAbsolutePath)));
        return false;
    }

    // The destination's parent, not the destination itself: creating the
    // destination would make the rename below fail against it.
    const QString targetParent = QFileInfo(targetAbsolutePath).absolutePath();
    if (!QDir().mkpath(targetParent)) {
        setError(errorString, QStringLiteral("Could not create the folder:\n%1")
                                  .arg(nativePath(targetParent)));
        return false;
    }

    // Fast path first. QDir::rename() does not copy across volumes — it simply
    // fails — so a false here is not yet a failure to report.
    if (QDir().rename(sourceAbsolutePath, targetAbsolutePath)) {
        return true;
    }

    if (!FileTree::copyRecursively(sourceAbsolutePath, targetAbsolutePath, errorString)) {
        // The source is untouched, so taking the partial destination back out
        // leaves the vessel exactly where it was (§2.5).
        FileTree::removeAnything(targetAbsolutePath);
        return false;
    }

    // Everything is now at the destination. A source that refuses to go away
    // — a file open in another program is the usual reason — is reported as a
    // warning and not as a failure: see the header for why failing here would
    // be the worse of the two outcomes.
    if (!FileTree::removeAnything(sourceAbsolutePath)) {
        qWarning() << "Moved the vessel's documents but could not remove the old copy at"
                   << nativePath(sourceAbsolutePath);
    }

    return true;
}

QStringList ManagedFileStore::entriesIn(const QString& directory)
{
    const QDir dir(directory);
    if (directory.isEmpty() || !dir.exists()) {
        return {};
    }

    return dir.entryList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System
                         | QDir::NoDotAndDotDot);
}

bool ManagedFileStore::moveNamedEntries(const QString&     sourceDir,
                                        const QString&     targetDir,
                                        const QStringList& names,
                                        QString*           errorString)
{
    setError(errorString, QString());

    if (names.isEmpty()) {
        return true; // nothing to move, which is not a failure
    }

    if (sourceDir.isEmpty() || targetDir.isEmpty()) {
        setError(errorString, QStringLiteral("No folder was given to move."));
        return false;
    }

    // Moving a folder into itself, or into something inside itself, would
    // consume its own source as it went.
    if (FileTree::isSameOrInside(targetDir, sourceDir)
        || FileTree::isSameOrInside(sourceDir, targetDir)) {
        setError(errorString,
                 QStringLiteral("These two folders are inside one another, so the documents "
                                "cannot be moved between them:\n\n%1\n\n%2")
                     .arg(nativePath(sourceDir), nativePath(targetDir)));
        return false;
    }

    if (!QDir().mkpath(targetDir)) {
        setError(errorString,
                 QStringLiteral("Could not create the folder:\n%1").arg(nativePath(targetDir)));
        return false;
    }

    // Checked in full before anything moves. A collision found half way would
    // leave the vessel's documents split across two folders, which is the one
    // state §2.5 exists to make impossible.
    for (const QString& name : names) {
        if (QFileInfo::exists(QDir(targetDir).filePath(name))) {
            setError(errorString,
                     QStringLiteral("This folder already contains \"%1\", so the vessel's "
                                    "documents cannot be moved into it:\n\n%2\n\nChoose a "
                                    "different folder.")
                         .arg(name, nativePath(targetDir)));
            return false;
        }
    }

    QStringList moved;
    for (const QString& name : names) {
        const QString from = QDir(sourceDir).filePath(name);
        const QString to   = QDir(targetDir).filePath(name);

        if (!QFileInfo::exists(from)) {
            continue; // named but no longer there: nothing to carry across
        }

        QString moveError;
        if (moveTree(from, to, &moveError)) {
            moved.append(name);
            continue;
        }

        // Put back what has already gone across, so the failure leaves one
        // folder holding everything rather than two holding half each.
        QStringList couldNotRestore;
        for (const QString& done : moved) {
            QString restoreError;
            if (!moveTree(QDir(targetDir).filePath(done), QDir(sourceDir).filePath(done),
                          &restoreError)) {
                couldNotRestore.append(done);
            }
        }

        if (couldNotRestore.isEmpty()) {
            setError(errorString, moveError);
        } else {
            // Worth saying out loud rather than summarising: some documents
            // are now at the new folder and the vessel is still recorded
            // against the old one.
            setError(errorString,
                     QStringLiteral("%1\n\nThese could not be moved back either, and are now "
                                    "in\n%2\n\n%3")
                         .arg(moveError, nativePath(targetDir),
                              couldNotRestore.join(QStringLiteral(", "))));
        }
        return false;
    }

    return true;
}
