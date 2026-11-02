#include "modules/certificates/service/CertificateDeletionService.h"

#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/service/CertificateAttachmentService.h"

#include <QDebug>
#include <QStringList>

#include <optional>

CertificateDeletionService::CertificateDeletionService(
    CertificateRepository& certificates, CertificateExtensionRepository& extensions,
    CertificateAttachmentService& attachments)
    : m_certificates(certificates)
    , m_extensions(extensions)
    , m_attachments(attachments)
{
}

bool CertificateDeletionService::deleteCertificate(const Certificate& certificate)
{
    m_errorString.clear();

    // ---- The paths, before anything is written ----------------------------
    //
    // Both lookups go through joins that require a live certificate, so after
    // the cascade below neither of them can answer. Asking first is not an
    // optimisation; it is the only moment the question can be asked at all.
    const QString attachmentPath = m_attachments.currentRelativePath(certificate.id);

    std::optional<CertificateExtension> extension;
    if (!m_extensions.findActiveFor(certificate.id, &extension)) {
        // Not fatal. An unreadable extension means its document is not
        // deleted, which is the same leftover-file outcome a failed delete
        // produces, and refusing to delete the certificate over it would be a
        // worse answer than saying so.
        qWarning() << "Could not read the short-term extension before deleting"
                   << certificate.name << ":" << m_extensions.errorString();
    }
    const QString extensionPath =
        extension.has_value() ? extension->attachmentPath : QString();

    // ---- The database, all of it or none of it ----------------------------
    if (!m_certificates.softDeleteWithChildren(certificate.id)) {
        m_errorString = m_certificates.errorString();
        return false; // nothing written, nothing deleted, safe to try again
    }

    // ---- And only now the files (§4) --------------------------------------
    //
    // Collected rather than returned one at a time: a certificate can have
    // both a document of its own and an extension's, and a user told "one of
    // them could not be deleted" should be told which.
    QStringList leftBehind;

    if (!attachmentPath.isEmpty()
        && !m_attachments.deleteStoredFile(certificate.vesselId, attachmentPath)) {
        leftBehind.append(m_attachments.errorString());
    }

    if (!extensionPath.isEmpty()
        && !m_attachments.deleteStoredFile(certificate.vesselId, extensionPath)) {
        leftBehind.append(m_attachments.errorString());
    }

    if (!leftBehind.isEmpty()) {
        qWarning() << "Deleted" << certificate.name
                   << "but could not delete all of its files:" << leftBehind;
        m_errorString = leftBehind.join(QStringLiteral("\n\n"));
    }

    return true;
}

QString CertificateDeletionService::errorString() const
{
    return m_errorString;
}
