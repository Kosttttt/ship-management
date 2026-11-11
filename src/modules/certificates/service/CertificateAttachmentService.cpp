#include "modules/certificates/service/CertificateAttachmentService.h"

#include "core/AppSettingRepository.h"
#include "core/VesselAttachmentRoots.h"
#include "modules/certificates/data/CertificateAttachmentRepository.h"
#include "modules/certificates/data/CertificateAttachmentStorage.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/AttachmentNaming.h"
#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/CertificateState.h"

#include <optional>

CertificateAttachmentService::CertificateAttachmentService(
    CertificateAttachmentRepository& repository, CertificateAttachmentStorage& storage,
    VesselAttachmentRoots& roots, EndorsementRepository& endorsements,
    CertificateExtensionRepository& extensions, AppSettingRepository& appSettings)
    : m_repository(repository)
    , m_storage(storage)
    , m_roots(roots)
    , m_endorsements(endorsements)
    , m_extensions(extensions)
    , m_appSettings(appSettings)
{
}

QDate CertificateAttachmentService::nameDateFor(const Certificate& certificate)
{
    // Each of the three reads below treats a failure as "nothing there", the
    // same judgment the list screen already makes for an unreadable
    // endorsement or extension: the file still gets attached, named from
    // slightly less information.
    QList<Endorsement> endorsements;
    m_endorsements.list(certificate.id, &endorsements);

    std::optional<CertificateExtension> extension;
    m_extensions.findActiveFor(certificate.id, &extension);
    const std::optional<QDate> extensionExpiry =
        extension.has_value() ? std::optional<QDate>(extension->expiryDate) : std::nullopt;

    AppSetting      setting;
    AlertThresholds thresholds;
    if (m_appSettings.read(&setting)) {
        thresholds.criticalDays     = setting.criticalDays;
        thresholds.expiringSoonDays = setting.expiringSoonDays;
        thresholds.dueSoonDays      = setting.dueSoonDays;
    }

    const CertificateState state = computeCertificateState(
        certificate, endorsements, thresholds, QDate::currentDate(), extensionExpiry);

    // Invalid only for a certificate that never expires and has no extension,
    // which has no date in its filename either way — so handing the invalid
    // date straight to buildName() is correct rather than a gap.
    return state.dueDate.isValid() ? state.dueDate : certificate.expiryDate;
}

bool CertificateAttachmentService::replaceCurrent(const Certificate& certificate,
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
        AttachmentNaming::buildName(certificate, nameDateFor(certificate),
                                    /*isShortTerm=*/false),
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

bool CertificateAttachmentService::deleteCurrent(const Certificate& certificate)
{
    m_errorString.clear();

    std::optional<CertificateAttachment> current;
    if (!m_repository.findActiveFor(certificate.id, &current)) {
        m_errorString = m_repository.errorString();
        return false;
    }
    if (!current.has_value()) {
        return true; // nothing attached: nothing to do, and not a failure
    }

    // Found before the row is retired, because afterwards there is nothing
    // left to say which file this was.
    const QString relativePath = current->relativePath;

    // The row first (§4's ordering): a failure here must not find the file
    // already gone, so the action stays safe to repeat.
    if (!m_repository.detach(certificate.id)) {
        m_errorString = m_repository.errorString();
        return false;
    }

    // And now the file itself, for good. The return value is deliberately
    // ignored: a failure leaves its message in errorString() for the caller to
    // surface, but does not put the row back. The certificate shows no
    // document, which is what was asked for, and a leftover file is a smaller
    // problem than a row claiming a document the user has been told is gone.
    deleteStoredFile(certificate.vesselId, relativePath);

    return true;
}

bool CertificateAttachmentService::deleteStoredFile(const QString& vesselId,
                                                    const QString& relativePath)
{
    m_errorString.clear();

    if (relativePath.isEmpty()) {
        return true; // nothing recorded, nothing to delete
    }

    // Where the file actually is, not where a new one would go: a document
    // stored before the per-vessel addendum sits under the installation's own
    // folder, and must be deleted from there (§2.2).
    const QString root = m_roots.rootHolding(vesselId, relativePath);
    if (root.isEmpty()) {
        return true; // already gone from everywhere this knows to look
    }

    if (!m_storage.deleteStored(root, relativePath, &m_errorString)) {
        return false;
    }
    return true;
}

QString CertificateAttachmentService::currentRelativePath(const QString& certificateId)
{
    m_errorString.clear();

    std::optional<CertificateAttachment> current;
    if (!m_repository.findActiveFor(certificateId, &current)) {
        m_errorString = m_repository.errorString();
        return QString();
    }
    return current.has_value() ? current->relativePath : QString();
}

QString CertificateAttachmentService::currentAbsolutePath(const QString& vesselId,
                                                          const QString& certificateId)
{
    const QString relativePath = currentRelativePath(certificateId);
    if (relativePath.isEmpty()) {
        return QString();
    }

    // The stored path is relative; this is the one place it becomes absolute,
    // and only so that something can be opened with it.
    return m_roots.resolveForOpening(vesselId, relativePath);
}

QString CertificateAttachmentService::errorString() const
{
    return m_errorString;
}

QString CertificateAttachmentService::vesselAttachmentRoot(const QString& vesselId)
{
    return m_roots.configuredRoot(vesselId);
}
