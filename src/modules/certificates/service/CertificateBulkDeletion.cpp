#include "modules/certificates/service/CertificateBulkDeletion.h"

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/service/CertificateDeletionService.h"

namespace {

QString describe(const Certificate& certificate, const QString& reason)
{
    return QStringLiteral("%1: %2").arg(certificate.name, reason);
}

} // namespace

BulkDeletionOutcome deleteCertificates(CertificateDeletionService& deletion,
                                       const QList<Certificate>&   certificates)
{
    BulkDeletionOutcome outcome;

    for (const Certificate& certificate : certificates) {
        if (!deletion.deleteCertificate(certificate)) {
            // Nothing at all happened to this one, so it is safe to try again
            // later - which is exactly what the reported name is for.
            outcome.failures.append(describe(certificate, deletion.errorString()));
            continue;
        }

        outcome.deletedNames.append(certificate.name);

        // Deleted, but a file would not go. Success with a caveat, recorded
        // apart from the failures so the summary can say so without implying
        // the certificate is still there.
        if (!deletion.errorString().isEmpty()) {
            outcome.fileLeftovers.append(describe(certificate, deletion.errorString()));
        }
    }

    return outcome;
}
