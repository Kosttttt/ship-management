#include "modules/certificates/data/CertificateAttachmentStorage.h"

#include "core/ManagedFileStore.h"

const QString CertificateAttachmentStorage::kArchiveSubfolder = QStringLiteral("archive");

namespace {

// An empty destination means the root folder itself, which under §3.1 is
// exactly where a vessel's documents go. Written as a named constant rather
// than a bare QString() at three call sites, so the flatness is deliberate
// where it is read rather than looking like a forgotten argument.
QString vesselFolderItself()
{
    return QString();
}

bool refuse(QString* errorString, const QString& text)
{
    if (errorString != nullptr) {
        *errorString = text;
    }
    return false;
}

// The two checks every entry point here makes. The service refuses before
// reaching any of them; these are the backstop, not the message a user
// normally sees.
bool guard(const QString& root, const QString& certificateId, QString* errorString)
{
    if (certificateId.isEmpty()) {
        return refuse(errorString, QStringLiteral("No certificate was given to attach this to."));
    }
    if (root.isEmpty()) {
        // Never a fallback to anywhere else (§2.2).
        return refuse(errorString,
                      QStringLiteral("No attachment folder is set for this vessel."));
    }
    return true;
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
    if (!guard(root, certificateId, errorString)) {
        return false;
    }

    // Archiving first, per §6: the old file leaves the folder before the new
    // one arrives, so the new copy gets the clean generated name rather than
    // a "_2" suffix earned from the document it is replacing.
    if (!currentRelativePath.isEmpty()
        && !ManagedFileStore::moveToArchive(root, currentRelativePath, kArchiveSubfolder,
                                            errorString)) {
        return false;
    }

    return ManagedFileStore::copyInto(root, sourcePath, vesselFolderItself(), desiredName,
                                      newRelativePathOut, errorString);
}

bool CertificateAttachmentStorage::addExtensionDocument(const QString& root,
                                                        const QString& certificateId,
                                                        const QString& sourcePath,
                                                        const QString& desiredName,
                                                        QString*       newRelativePathOut,
                                                        QString*       errorString)
{
    if (!guard(root, certificateId, errorString)) {
        return false;
    }

    // Beside the certificate's own document rather than under it (§3.1): the
    // _ShortTerm_ already in desiredName is what tells the two apart, and the
    // numeric suffix ManagedFileStore adds on a collision covers the one case
    // two documents can share a name — the same certificate extended twice to
    // the same expiry date.
    return ManagedFileStore::copyInto(root, sourcePath, vesselFolderItself(), desiredName,
                                      newRelativePathOut, errorString);
}

bool CertificateAttachmentStorage::deleteStored(const QString& root,
                                                const QString& relativePath,
                                                QString*       errorString)
{
    if (root.isEmpty()) {
        return refuse(errorString,
                      QStringLiteral("No attachment folder is set for this vessel."));
    }
    return ManagedFileStore::deleteAt(root, relativePath, errorString);
}
