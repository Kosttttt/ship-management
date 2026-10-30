#pragma once

#include "modules/certificates/domain/CertificateAttachment.h"

#include <QString>

#include <optional>

class InstallationContext;
class QSqlDatabase;

// Reads and writes certificate attachments (certificate-attachment-spec §7).
//
// SQL only. This class never touches the filesystem — moving and copying the
// actual files is CertificateAttachmentStorage's job, and keeping the two apart
// is what makes each of them testable on its own.
//
// Mirrors CertificateExtensionRepository, including its one structural quirk:
// an attachment has no vessel_id of its own, so the VESSEL-mode scope filter is
// applied transitively, through the certificate the attachment belongs to.
class CertificateAttachmentRepository
{
public:
    CertificateAttachmentRepository(QSqlDatabase&              database,
                                    const InstallationContext& installation);

    // The one active (not-yet-replaced) attachment for a certificate, if any.
    // Another vessel's certificate returns nothing rather than an error — the
    // established "not found is a successful answer" precedent, which also
    // keeps this safe to call per row from a list reload.
    bool findActiveFor(const QString&                        certificateId,
                       std::optional<CertificateAttachment>* attachment);

    // Soft-deletes whatever is currently active for this certificate, if any,
    // and inserts a new row for relativePath — one call rather than the
    // separate start()/resolve() an extension needs, because "replace" is the
    // only thing anything ever asks for here (§7).
    //
    // relativePath must already be relative to the managed data folder: it is
    // the path CertificateAttachmentStorage reports having actually used, and
    // storing an absolute one would break CLAUDE.md §6.9.
    bool attach(const QString& certificateId, const QString& relativePath,
                QString* newId = nullptr);

    QString errorString() const;

private:
    // The parent's vessel id, with scope applied. Sets *ok to false and fills
    // m_errorString when the certificate is missing or out of scope.
    QString vesselIdForCertificate(const QString& certificateId, bool* ok);

    bool isScopedToOneVessel() const;

    QSqlDatabase&              m_database;
    const InstallationContext& m_installation;
    QString                    m_errorString;
};
