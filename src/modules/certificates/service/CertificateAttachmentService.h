#pragma once

#include <QDate>
#include <QString>

struct Certificate;

class CertificateAttachmentRepository;
class CertificateAttachmentStorage;
class VesselAttachmentRoots;

// The attachment workflow (certificate-attachment-spec §7): find the vessel's
// folder, name the file, archive what it replaces, copy the new one in, then
// record the path.
//
// The service layer, because this is a sequence of steps whose *order* is the
// rule — filesystem first, database second, so that the failure mode is an
// orphaned copy on disk rather than a database row pointing at nothing. Four
// screens have to perform that sequence identically (the list's Attachment
// menu, the Add/Edit dialog, the renewal dialog and the extension dialog), and
// a rule duplicated in four widgets is a rule that will eventually be followed
// in only three of them.
//
// It is also the one place the addendum's refusal lives
// (certificate-attachment-addendum-spec §2.2): a vessel with no folder
// configured cannot have anything attached to its certificates, and the
// message says where to set one. Putting the check here rather than in each
// screen is what makes "in all four places" true by construction instead of by
// inspection.
//
// No widgets and no SQL of its own: it asks VesselAttachmentRoots where the
// files go, CertificateAttachmentRepository for the rows, and
// CertificateAttachmentStorage for the files themselves.
class CertificateAttachmentService
{
public:
    CertificateAttachmentService(CertificateAttachmentRepository& repository,
                                 CertificateAttachmentStorage&    storage,
                                 VesselAttachmentRoots&           roots);

    // Makes sourcePath the certificate's current attachment, archiving
    // whatever was there. expiryDateForName is the date the generated filename
    // carries: the certificate's own expiry date normally, and its *new* expiry
    // date when this runs as part of a renewal (§8).
    //
    // sourceIsFolder comes from which action the user chose — "Attach File" or
    // "Attach Folder" — rather than from inspecting the path, so the naming
    // stays a pure decision.
    //
    // The vessel comes from certificate.vesselId, which every caller already
    // has in hand: there is no screen that can attach something without first
    // having loaded the certificate it is attaching it to (§2.4).
    bool replaceCurrent(const Certificate& certificate,
                        const QDate&       expiryDateForName,
                        const QString&     sourcePath,
                        bool               sourceIsFolder);

    // An extension's own document (§2): copied in, nothing archived, and the
    // stored path handed back for the caller to put on the extension row it is
    // about to insert. Nothing here touches certificate_attachment.
    //
    // Takes the whole certificate rather than the extension because a
    // CertificateExtension carries no vessel id of its own, and the stored
    // filename needs the certificate's number and name anyway.
    bool addExtensionDocument(const Certificate& certificate,
                              const QDate&       extensionExpiry,
                              const QString&     sourcePath,
                              bool               sourceIsFolder,
                              QString*           relativePathOut);

    // The current attachment as an absolute path on this machine, for opening
    // it. Empty when the certificate has none — which is also how the list
    // decides whether to offer "Open Current Attachment" at all, and whether
    // to put an indicator in the attachment column.
    //
    // Opening is not gated on the vessel having a folder configured (§2.2): an
    // attachment made before the addendum shipped still resolves, through
    // VesselAttachmentRoots::resolveForOpening().
    QString currentAbsolutePath(const QString& vesselId, const QString& certificateId);

    QString errorString() const;

private:
    CertificateAttachmentRepository& m_repository;
    CertificateAttachmentStorage&    m_storage;
    VesselAttachmentRoots&           m_roots;
    QString                          m_errorString;
};
