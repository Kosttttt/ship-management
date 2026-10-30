#include "modules/certificates/domain/AttachmentNaming.h"

#include "modules/certificates/domain/Certificate.h"

#include <QStringList>

namespace {

// The characters Windows refuses in a filename. The other two platforms refuse
// fewer, so obeying the strictest of the three makes one generated name legal
// everywhere — which matters because the managed folder is copied between
// machines (CLAUDE.md §6.9).
const QString kForbidden = QStringLiteral("<>:\"/\\|?*");

// Everything the user typed is a candidate for a filename here, so this has to
// assume nothing about it: a certificate called `Safety "Equipment"/Hull` is
// perfectly reasonable to type and completely illegal to write to disk.
QString sanitised(const QString& value)
{
    QString result;
    result.reserve(value.size());

    bool whitespacePending = false;
    for (const QChar character : value) {
        if (character.isSpace()) {
            // Collapsed, not dropped: "Safety Construction" must not become
            // "SafetyConstruction". Leading whitespace produces nothing,
            // because result is still empty.
            whitespacePending = !result.isEmpty();
            continue;
        }
        if (kForbidden.contains(character) || character.category() == QChar::Other_Control) {
            continue;
        }
        if (whitespacePending) {
            result.append(QLatin1Char('_'));
            whitespacePending = false;
        }
        result.append(character);
    }

    return result;
}

} // namespace

QString AttachmentNaming::buildName(const Certificate& certificate,
                                    const QDate&       expiryDateUsed,
                                    bool               isShortTerm)
{
    // Assembled from the parts that are actually present rather than from a
    // fixed template: the list number is optional (certificate-crud-spec §8.2),
    // and a certificate that never expires has no date to use. Joining only
    // non-empty parts avoids a name that starts or ends with a stray
    // underscore.
    QStringList parts;

    const QString listNumber = sanitised(certificate.listNumber);
    if (!listNumber.isEmpty()) {
        parts.append(listNumber);
    }

    const QString name = sanitised(certificate.name);
    if (!name.isEmpty()) {
        parts.append(name);
    }

    if (isShortTerm) {
        parts.append(QStringLiteral("ShortTerm"));
    }

    if (expiryDateUsed.isValid()) {
        // The calendar date as it is stored, YYYY-MM-DD (CLAUDE.md §6.3), so
        // the names of one certificate's documents sort chronologically.
        parts.append(expiryDateUsed.toString(Qt::ISODate));
    }

    QString result = parts.join(QLatin1Char('_'));

    // Windows silently drops a trailing dot from a filename, which would make
    // the name on disk differ from the one recorded in the database.
    while (result.endsWith(QLatin1Char('.'))) {
        result.chop(1);
    }

    // Only reachable if the certificate has no list number and a name made
    // entirely of illegal characters. A dull fallback beats an empty filename.
    return result.isEmpty() ? QStringLiteral("attachment") : result;
}

QString AttachmentNaming::withExtensionFrom(const QString& baseName,
                                            const QString& sourcePath,
                                            bool           sourceIsFolder)
{
    if (sourceIsFolder) {
        return baseName;
    }

    // Found by hand rather than with QFileInfo, which belongs to the layers
    // that are allowed to touch the filesystem. Only the part after the last
    // separator counts, so a dot in a folder name along the way is ignored,
    // and a leading dot is a hidden file rather than an extension.
    const int lastSlash = qMax(sourcePath.lastIndexOf(QLatin1Char('/')),
                               sourcePath.lastIndexOf(QLatin1Char('\\')));
    const QString fileName = sourcePath.mid(lastSlash + 1);

    const int lastDot = fileName.lastIndexOf(QLatin1Char('.'));
    if (lastDot <= 0 || lastDot == fileName.size() - 1) {
        return baseName;
    }

    return baseName + fileName.mid(lastDot);
}
