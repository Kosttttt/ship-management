#pragma once

#include "modules/certificates/domain/CertificateExtension.h"

#include <QString>

#include <optional>

class InstallationContext;
class QSqlDatabase;

// Reads and writes short-term extensions (certificate-extension-spec §5).
//
// Mirrors EndorsementRepository, including its one structural quirk: an
// extension has no vessel_id of its own, so the VESSEL-mode scope filter is
// applied transitively, through the certificate the extension belongs to.
class CertificateExtensionRepository
{
public:
    CertificateExtensionRepository(QSqlDatabase&              database,
                                   const InstallationContext& installation);

    // The one active (not-yet-resolved) extension for a certificate, if any.
    // Another vessel's certificate returns nothing rather than an error — the
    // established "not found is a successful answer" precedent, which also
    // keeps this safe to call per row from a list reload.
    bool findActiveFor(const QString& certificateId, std::optional<CertificateExtension>* extension);

    // Refuses with a friendly message if this certificate already has an
    // active extension, or if expiryDate is invalid — this is a write.
    bool start(const CertificateExtension& extension, QString* newId = nullptr);

    // Soft-deletes the active extension for this certificate, if any.
    // Affecting zero rows is success, not failure — nothing to resolve is a
    // normal case (the UI does not offer this with nothing active, but the
    // repository does not rely on the UI to enforce that).
    bool resolve(const QString& certificateId);

    QString errorString() const;

private:
    bool validate(const CertificateExtension& extension);

    // The parent's vessel id, with scope applied. Sets *ok to false and fills
    // m_errorString when the certificate is missing or out of scope.
    QString vesselIdForCertificate(const QString& certificateId, bool* ok);

    bool isScopedToOneVessel() const;

    QSqlDatabase&              m_database;
    const InstallationContext& m_installation;
    QString                    m_errorString;
};
