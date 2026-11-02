#include "core/FileTree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

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

} // namespace

QString FileTree::nameWithIndex(const QString& desiredName, int index)
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

QString FileTree::nameWithMarker(const QString& fileName,
                                 const QString& marker,
                                 bool           treatAsFolder)
{
    if (treatAsFolder) {
        return fileName + marker;
    }

    // QFileInfo rather than a hand-rolled search, because it already knows
    // that a leading dot is a hidden file and not an extension: ".htaccess"
    // has no suffix, so the marker goes on the end where it belongs.
    const QFileInfo info(fileName);
    const QString   suffix = info.suffix();
    if (suffix.isEmpty()) {
        return fileName + marker;
    }

    return info.completeBaseName() + marker + QLatin1Char('.') + suffix;
}

bool FileTree::removeAnything(const QString& absolutePath)
{
    const QFileInfo info(absolutePath);
    if (!info.exists()) {
        return true; // nothing to do, and nothing left behind
    }
    if (info.isDir()) {
        return QDir(absolutePath).removeRecursively();
    }
    return QFile::remove(absolutePath);
}

bool FileTree::copyRecursively(const QString& source,
                               const QString& destination,
                               QString*       errorString)
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

bool FileTree::isSameOrInside(const QString& path, const QString& possibleAncestor)
{
    if (path.isEmpty() || possibleAncestor.isEmpty()) {
        return false;
    }

    // cleanPath, not canonicalFilePath: the path being tested may not exist
    // yet — that is the usual case when a folder is about to be created — and
    // canonicalFilePath() returns an empty string for anything that does not.
    const QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString ancestor  = QDir::cleanPath(QFileInfo(possibleAncestor).absoluteFilePath());

    if (candidate == ancestor) {
        return true;
    }

    // The trailing separator matters: without it "/data/shipsy" would count as
    // being inside "/data/ships".
    return candidate.startsWith(ancestor + QLatin1Char('/'));
}
