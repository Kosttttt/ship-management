#include "modules/certificates/data/CertificateAttachmentStorage.h"

#include "core/ManagedFileStore.h"
#include "core/VesselAttachmentRoots.h"

namespace {

// The §4 layout, written down exactly once.
//
//   attachments/<certificateId>/              the current attachment
//   attachments/<certificateId>/archive/      what it replaced
//   attachments/<certificateId>/extensions/   each extension's own document
//
// "attachments" itself comes from core, because a vessel's folder changing has
// to move that subtree as a unit and core is what does the moving
// (certificate-attachment-addendum-spec §2.5). Everything below it is this
// module's business and appears nowhere else.
//
// Relative paths with forward slashes throughout, never a backslash and never
// an absolute path (CLAUDE.md §2 and §6.9).

QString certificateDir(const QString& certificateId)
{
    return VesselAttachmentRoots::kManagedSubfolder + QStringLiteral("/") + certificateId;
}

QString archiveDir(const QString& certificateId)
{
    return certificateDir(certificateId) + QStringLiteral("/archive");
}

QString extensionsDir(const QString& certificateId)
{
    return certificateDir(certificateId) + QStringLiteral("/extensions");
}

bool refuse(QString* errorString, const QString& text)
{
    if (errorString != nullptr) {
        *errorString = text;
    }
    return false;
}

} // namespace

bool CertificateAttachmentStorage::replaceCurrent(const QString& root,
                                                  const QString& certificateId,
                                                  const QString& currentRelativePath,
                                                  const QString& sourcePath,
                                                  const QString& desiredName,
                                                  QString*       newRelativePathOut,
                                                  QString*       errorString)
{
    if (certificateId.isEmpty()) {
        return refuse(errorString, QStringLiteral("No certificate was given to attach this to."));
    }
    if (root.isEmpty()) {
        // Never a fallback to anywhere else (§2.2). The service refuses before
        // reaching this point; this is the backstop, not the message a user
        // normally sees.
        return refuse(errorString,
                      QStringLiteral("No attachment folder is set for this vessel."));
    }

    // Archiving first, per §6: the old file leaves the folder before the new
    // one arrives, so the new copy gets the clean generated name rather than
    // a "_2" suffix earned from the document it is replacing.
    if (!currentRelativePath.isEmpty()
        && !ManagedFileStore::moveToArchive(root, currentRelativePath, archiveDir(certificateId),
                                            errorString)) {
        return false;
    }

    return ManagedFileStore::copyInto(root, sourcePath, certificateDir(certificateId), desiredName,
                                      newRelativePathOut, errorString);
}

bool CertificateAttachmentStorage::addExtensionDocument(const QString& root,
                                                        const QString& certificateId,
                                                        const QString& sourcePath,
                                                        const QString& desiredName,
                                                        QString*       newRelativePathOut,
                                                        QString*       errorString)
{
    if (certificateId.isEmpty()) {
        return refuse(errorString, QStringLiteral("No certificate was given to attach this to."));
    }
    if (root.isEmpty()) {
        return refuse(errorString,
                      QStringLiteral("No attachment folder is set for this vessel."));
    }

    // Flat, with no per-extension subfolder: the extension row is the archival
    // unit, so there is never anything to move once one resolves (§4). The
    // numeric suffix ManagedFileStore adds on a collision covers the one case
    // two documents can share a name — the same certificate extended twice to
    // the same expiry date.
    return ManagedFileStore::copyInto(root, sourcePath, extensionsDir(certificateId), desiredName,
                                      newRelativePathOut, errorString);
}
