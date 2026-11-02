#include "modules/certificates/service/CertificateAttachmentService.h"

#include "core/VesselAttachmentRoots.h"
#include "modules/certificates/data/CertificateAttachmentRepository.h"
#include "modules/certificates/data/CertificateAttachmentStorage.h"
#include "modules/certificates/domain/AttachmentNaming.h"
#include "modules/certificates/domain/Certificate.h"

#include <optional>

CertificateAttachmentService::CertificateAttachmentService(
    CertificateAttachmentRepository& repository, CertificateAttachmentStorage& storage,
    VesselAttachmentRoots& roots)
    : m_repository(repository)
    , m_storage(storage)
    , m_roots(roots)
{
}

bool CertificateAttachmentService::replaceCurrent(const Certificate& certificate,
                                                  const QDate&       expiryDateForName,
                                                  const QString&     sourcePath,
                                                  bool               sourceIsFolder)
{
    m_errorString.clear();

    // Step 0, new with the addendum: where this vessel's documents go. An
    // empty answer is a refusal and nothing else — there is no fallback to the
    // installation's own folder, which is what the old single-folder behaviour
    // would amount to (§2.2). Checked before anything is read or copied, so a
    // refusal cannot leave a trace.
    const QString root = m_roots.rootForWriting(certificate.vesselId, &m_errorString);
    if (root.isEmpty()) {
        return false;
    }

    // Step 1 of §7: what is attached now, so there is something to archive.
    std::optional<CertificateAttachment> current;
    if (!m_repository.findActiveFor(certificate.id, &current)) {
        m_errorString = m_repository.errorString();
        return false;
    }

    const QString desiredName = AttachmentNaming::withExtensionFrom(
        AttachmentNaming::buildName(certificate, expiryDateForName, /*isShortTerm=*/false),
        sourcePath, sourceIsFolder);

    // Step 2: the filesystem work, before the database write. A failure here
    // leaves the recorded attachment exactly as it was.
    QString newRelativePath;
    if (!m_storage.replaceCurrent(root, certificate.id,
                                  current.has_value() ? current->relativePath : QString(),
                                  sourcePath, desiredName, &newRelativePath, &m_errorString)) {
        return false;
    }

    // Step 3: record the path the copy actually landed on, which is not
    // necessarily desiredName — a collision earns a numeric suffix.
    //
    // Still relative, and still relative to a root rather than to one global
    // folder: the vessel's folder is what the root now is, so the same stored
    // string keeps working when that folder moves (§2.5 moves the files and
    // leaves every relative_path untouched).
    //
    // Deliberately not inside one transaction spanning the copy above: if this
    // fails, the result is an orphaned copy on disk, not a row pointing at
    // nothing. That is the way round this has to fail (§7).
    if (!m_repository.attach(certificate.id, newRelativePath)) {
        m_errorString = m_repository.errorString();
        return false;
    }

    return true;
}

bool CertificateAttachmentService::addExtensionDocument(const Certificate& certificate,
                                                        const QDate&       extensionExpiry,
                                                        const QString&     sourcePath,
                                                        bool               sourceIsFolder,
                                                        QString*           relativePathOut)
{
    m_errorString.clear();

    const QString root = m_roots.rootForWriting(certificate.vesselId, &m_errorString);
    if (root.isEmpty()) {
        return false;
    }

    // The extension's own expiry date, not the certificate's, and _ShortTerm_
    // in the name — this document is the short-term certificate, not the real
    // one (§2).
    const QString desiredName = AttachmentNaming::withExtensionFrom(
        AttachmentNaming::buildName(certificate, extensionExpiry, /*isShortTerm=*/true),
        sourcePath, sourceIsFolder);

    return m_storage.addExtensionDocument(root, certificate.id, sourcePath, desiredName,
                                          relativePathOut, &m_errorString);
}

QString CertificateAttachmentService::currentAbsolutePath(const QString& vesselId,
                                                          const QString& certificateId)
{
    m_errorString.clear();

    std::optional<CertificateAttachment> current;
    if (!m_repository.findActiveFor(certificateId, &current)) {
        m_errorString = m_repository.errorString();
        return QString();
    }
    if (!current.has_value()) {
        return QString();
    }

    // The stored path is relative; this is the one place it becomes absolute,
    // and only so that something can be opened with it.
    return m_roots.resolveForOpening(vesselId, current->relativePath);
}

QString CertificateAttachmentService::errorString() const
{
    return m_errorString;
}
