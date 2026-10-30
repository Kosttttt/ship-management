#include "modules/certificates/service/CertificateAttachmentService.h"

#include "core/ManagedFileStore.h"
#include "modules/certificates/data/CertificateAttachmentRepository.h"
#include "modules/certificates/data/CertificateAttachmentStorage.h"
#include "modules/certificates/domain/AttachmentNaming.h"
#include "modules/certificates/domain/Certificate.h"

#include <optional>

CertificateAttachmentService::CertificateAttachmentService(
    CertificateAttachmentRepository& repository, CertificateAttachmentStorage& storage)
    : m_repository(repository)
    , m_storage(storage)
{
}

bool CertificateAttachmentService::replaceCurrent(const Certificate& certificate,
                                                  const QDate&       expiryDateForName,
                                                  const QString&     sourcePath,
                                                  bool               sourceIsFolder)
{
    m_errorString.clear();

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
    if (!m_storage.replaceCurrent(certificate.id,
                                  current.has_value() ? current->relativePath : QString(),
                                  sourcePath, desiredName, &newRelativePath, &m_errorString)) {
        return false;
    }

    // Step 3: record the path the copy actually landed on, which is not
    // necessarily desiredName — a collision earns a numeric suffix.
    //
    // Deliberately not inside one transaction spanning the move above: if this
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

    // The extension's own expiry date, not the certificate's, and _ShortTerm_
    // in the name — this document is the short-term certificate, not the real
    // one (§2).
    const QString desiredName = AttachmentNaming::withExtensionFrom(
        AttachmentNaming::buildName(certificate, extensionExpiry, /*isShortTerm=*/true),
        sourcePath, sourceIsFolder);

    return m_storage.addExtensionDocument(certificate.id, sourcePath, desiredName,
                                          relativePathOut, &m_errorString);
}

QString CertificateAttachmentService::currentAbsolutePath(const QString& certificateId)
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
    return ManagedFileStore::resolveAbsolute(current->relativePath);
}

QString CertificateAttachmentService::errorString() const
{
    return m_errorString;
}
