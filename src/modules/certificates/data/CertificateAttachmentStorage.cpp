#include "modules/certificates/data/CertificateAttachmentStorage.h"

#include "core/ManagedFileStore.h"

namespace {

// The §4 layout, written down exactly once.
//
//   attachments/<certificateId>/              the current attachment
//   attachments/<certificateId>/archive/      what it replaced
//   attachments/<certificateId>/extensions/   each extension's own document
//
// Relative paths with forward slashes throughout, never a backslash and never
// an absolute path (CLAUDE.md §2 and §6.9).

QString certificateDir(const QString& certificateId)
{
    return QStringLiteral("attachments/%1").arg(certificateId);
}

QString archiveDir(const QString& certificateId)
{
    return certificateDir(certificateId) + QStringLiteral("/archive");
}

QString extensionsDir(const QString& certificateId)
{
    return certificateDir(certificateId) + QStringLiteral("/extensions");
}

} // namespace

bool CertificateAttachmentStorage::replaceCurrent(const QString& certificateId,
                                                  const QString& currentRelativePath,
                                                  const QString& sourcePath,
                                                  const QString& desiredName,
                                                  QString*       newRelativePathOut,
                                                  QString*       errorString)
{
    if (certificateId.isEmpty()) {
        if (errorString != nullptr) {
            *errorString = QStringLiteral("No certificate was given to attach this to.");
        }
        return false;
    }

    // Archiving first, per §6: the old file leaves the folder before the new
    // one arrives, so the new copy gets the clean generated name rather than
    // a "_2" suffix earned from the document it is replacing.
    if (!currentRelativePath.isEmpty()
        && !ManagedFileStore::moveToArchive(currentRelativePath, archiveDir(certificateId),
                                            errorString)) {
        return false;
    }

    return ManagedFileStore::copyInto(sourcePath, certificateDir(certificateId), desiredName,
                                      newRelativePathOut, errorString);
}

bool CertificateAttachmentStorage::addExtensionDocument(const QString& certificateId,
                                                        const QString& sourcePath,
                                                        const QString& desiredName,
                                                        QString*       newRelativePathOut,
                                                        QString*       errorString)
{
    if (certificateId.isEmpty()) {
        if (errorString != nullptr) {
            *errorString = QStringLiteral("No certificate was given to attach this to.");
        }
        return false;
    }

    // Flat, with no per-extension subfolder: the extension row is the archival
    // unit, so there is never anything to move once one resolves (§4). The
    // numeric suffix ManagedFileStore adds on a collision covers the one case
    // two documents can share a name — the same certificate extended twice to
    // the same expiry date.
    return ManagedFileStore::copyInto(sourcePath, extensionsDir(certificateId), desiredName,
                                      newRelativePathOut, errorString);
}
