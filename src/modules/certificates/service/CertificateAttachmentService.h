#pragma once

#include <QDate>
#include <QString>

struct Certificate;

class CertificateAttachmentRepository;
class CertificateAttachmentStorage;

// The attachment workflow (certificate-attachment-spec §7): name the file,
// archive what it replaces, copy the new one in, then record the path.
//
// The service layer, because this is a sequence of steps whose *order* is the
// rule — filesystem first, database second, so that the failure mode is an
// orphaned copy on disk rather than a database row pointing at nothing. Two
// screens have to perform that sequence identically (the list's Attachment
// menu and the renewal dialog), and a rule duplicated in two widgets is a rule
// that will eventually be followed in only one of them.
//
// No widgets and no SQL of its own: it asks
// CertificateAttachmentRepository for the rows and
// CertificateAttachmentStorage for the files.
class CertificateAttachmentService
{
public:
    CertificateAttachmentService(CertificateAttachmentRepository& repository,
                                 CertificateAttachmentStorage&    storage);

    // Makes sourcePath the certificate's current attachment, archiving
    // whatever was there. expiryDateForName is the date the generated filename
    // carries: the certificate's own expiry date normally, and its *new* expiry
    // date when this runs as part of a renewal (§8).
    //
    // sourceIsFolder comes from which action the user chose — "Attach File" or
    // "Attach Folder" — rather than from inspecting the path, so the naming
    // stays a pure decision.
    bool replaceCurrent(const Certificate& certificate,
                        const QDate&       expiryDateForName,
                        const QString&     sourcePath,
                        bool               sourceIsFolder);

    // An extension's own document (§2): copied in, nothing archived, and the
    // stored path handed back for the caller to put on the extension row it is
    // about to insert. Nothing here touches certificate_attachment.
    bool addExtensionDocument(const Certificate& certificate,
                              const QDate&       extensionExpiry,
                              const QString&     sourcePath,
                              bool               sourceIsFolder,
                              QString*           relativePathOut);

    // The current attachment as an absolute path on this machine, for opening
    // it. Empty when the certificate has none — which is also how the list
    // decides whether to offer "Open Current Attachment" at all.
    QString currentAbsolutePath(const QString& certificateId);

    QString errorString() const;

private:
    CertificateAttachmentRepository& m_repository;
    CertificateAttachmentStorage&    m_storage;
    QString                          m_errorString;
};
