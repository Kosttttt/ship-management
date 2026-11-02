#pragma once

#include <QString>

struct Certificate;

class CertificateAttachmentService;
class CertificateExtensionRepository;
class CertificateRepository;

// Deleting a certificate for good (certificate-attachment-addendum-2-spec §4):
// the row and its children soft-deleted, and the documents it still has live
// paths for genuinely removed from disk.
//
// The service layer, for the same reason the attachment workflow is: the rule
// is the *order*. Here it runs the opposite way round from attaching —
// database first, filesystem second — because a failure to delete the row must
// not find the document already destroyed, whereas a failure to record an
// attachment is better than a row pointing at nothing. Two opposite orders for
// two opposite operations, each written down once.
//
// It is deliberately small: the cascade itself is one transaction inside
// CertificateRepository, because SQL belongs in the repository layer and a
// cascade belongs with the parent table. What is left here is the part no
// repository can do — knowing which files to delete, and that they are deleted
// after the rows and not before.
//
// Known limitation, accepted rather than solved (§4 and §10): older copies of
// this certificate's documents already sitting in the shared archive/ folder
// are left alone. Once a replaced file has been renamed into that folder there
// is no reliable way back from the file to the certificate it came from, and
// guessing from the filename would eventually delete somebody else's document.
// Only the files the database still has a live path for are removed.
class CertificateDeletionService
{
public:
    CertificateDeletionService(CertificateRepository&          certificates,
                               CertificateExtensionRepository& extensions,
                               CertificateAttachmentService&   attachments);

    // §4, in order: find the paths, soft-delete the certificate with its
    // endorsements, extension and attachment row, then delete the two
    // documents the database still names.
    //
    // Returns false only when the database step failed, in which case nothing
    // at all has happened and the action is safe to repeat. A file that could
    // not be deleted afterwards returns true with errorString() set: the
    // certificate is gone, which is what was asked for, and the leftover file
    // is reported rather than hidden — the same call step 10 made for a
    // renewal whose survey clear failed.
    bool deleteCertificate(const Certificate& certificate);

    QString errorString() const;

private:
    CertificateRepository&          m_certificates;
    CertificateExtensionRepository& m_extensions;
    CertificateAttachmentService&   m_attachments;
    QString                         m_errorString;
};
