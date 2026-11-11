#pragma once

#include <QDate>
#include <QString>

struct Certificate;

class AppSettingRepository;
class CertificateAttachmentRepository;
class CertificateAttachmentStorage;
class CertificateExtensionRepository;
class EndorsementRepository;
class VesselAttachmentRoots;

// The attachment workflow (certificate-attachment-spec §7): find the vessel's
// folder, name the file, archive what it replaces, copy the new one in, then
// record the path. And, since certificate-attachment-addendum-2-spec §5, the
// reverse: forget the row and delete the file for good.
//
// The service layer, because this is a sequence of steps whose *order* is the
// rule — filesystem first, database second, so that the failure mode is an
// orphaned copy on disk rather than a database row pointing at nothing. Four
// screens have to perform that sequence identically (the list's Attachment
// menu, the Add/Edit dialog, the renewal dialog and the extension dialog), and
// a rule duplicated in four widgets is a rule that will eventually be followed
// in only three of them.
//
// A deletion runs the other way round — the row first, then the file — and the
// reason is the same one stated backwards: a failed delete of the row must not
// find the file already destroyed (§4).
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
// CertificateAttachmentStorage for the files themselves. It reads endorsements,
// extensions and the alert thresholds only to work out which date an attached
// file should be named after (addendum 2 §7).
class CertificateAttachmentService
{
public:
    CertificateAttachmentService(CertificateAttachmentRepository& repository,
                                 CertificateAttachmentStorage&    storage,
                                 VesselAttachmentRoots&           roots,
                                 EndorsementRepository&           endorsements,
                                 CertificateExtensionRepository&  extensions,
                                 AppSettingRepository&            appSettings);

    // Makes sourcePath the certificate's current attachment, archiving
    // whatever was there.
    //
    // The generated filename carries whichever of the certificate's expiry
    // date, its next annual survey window and its next intermediate survey
    // window is soonest — the same date the list's Days Left column counts to
    // (addendum 2 §7), worked out here rather than passed in. It used to be
    // passed in, as "the certificate's expiry date, or its new one on a
    // renewal"; the certificate handed over is the row as it now stands, so
    // its own dates are already the right ones to read in all three modes and
    // the parameter had become a chance to pass the wrong date.
    //
    // sourceIsFolder comes from which action the user chose — "Attach File" or
    // "Attach Folder" — rather than from inspecting the path, so the naming
    // stays a pure decision.
    //
    // The vessel comes from certificate.vesselId, which every caller already
    // has in hand: there is no screen that can attach something without first
    // having loaded the certificate it is attaching it to (§2.4).
    bool replaceCurrent(const Certificate& certificate,
                        const QString&     sourcePath,
                        bool               sourceIsFolder);

    // An extension's own document (§2): copied in, nothing archived, and the
    // stored path handed back for the caller to put on the extension row it is
    // about to insert. Nothing here touches certificate_attachment.
    //
    // extensionExpiry stays a parameter, unlike replaceCurrent's date above:
    // the extension does not exist yet when this runs, so its expiry cannot be
    // read back from anywhere.
    //
    // Takes the whole certificate rather than the extension because a
    // CertificateExtension carries no vessel id of its own, and the stored
    // filename needs the certificate's number and name anyway.
    bool addExtensionDocument(const Certificate& certificate,
                              const QDate&       extensionExpiry,
                              const QString&     sourcePath,
                              bool               sourceIsFolder,
                              QString*           relativePathOut);

    // addendum 2 §5: forgets the current attachment and deletes its file for
    // good — no archive copy, because this exists to correct a mistake rather
    // than to record a supersession.
    //
    // Returns false only when the database step failed, in which case nothing
    // has been destroyed and the action can simply be tried again. A file that
    // could not be deleted after the row was already retired returns true with
    // errorString() set: the certificate shows no document, which is what was
    // asked for, and the leftover file is reported rather than hidden (§4's
    // ordering rule, applied here too).
    //
    // A certificate with nothing attached is success with nothing done, the
    // same judgment CertificateExtensionRepository::resolve() already makes.
    bool deleteCurrent(const Certificate& certificate);

    // Deletes one stored file belonging to a certificate, by the relative path
    // some other row recorded — an extension's own document, for §4's cascade.
    // The row is that repository's business; only the file is this one's.
    //
    // Resolved against whichever root the file is actually under, so a
    // document stored before the per-vessel addendum is deleted from where it
    // really is rather than from where a new one would go.
    bool deleteStoredFile(const QString& vesselId, const QString& relativePath);

    // The current attachment's stored relative path, or empty when there is
    // none. Needed by §4, which has to know what to delete before the cascade
    // soft-deletes the row that names it.
    QString currentRelativePath(const QString& certificateId);

    // The current attachment as an absolute path on this machine, for opening
    // it. Empty when the certificate has none — which is also how the list
    // decides whether to offer "Open Current Attachment" at all, and whether
    // to put an indicator in the attachment column.
    //
    // Opening is not gated on the vessel having a folder configured (§2.2): an
    // attachment made before the addendum shipped still resolves, through
    // VesselAttachmentRoots::resolveForOpening().
    QString currentAbsolutePath(const QString& vesselId, const QString& certificateId);

    // The vessel's configured attachment folder, or empty when it has none.
    // Exposed for the file chooser, which must never open inside the managed
    // folder (housekeeping-round-spec §5). A plain passthrough to
    // VesselAttachmentRoots, so a widget does not have to hold one of its own.
    QString vesselAttachmentRoot(const QString& vesselId);

    QString errorString() const;

private:
    // The date an attached file is named after (addendum 2 §7): the one
    // CertificateState derives for the Days Left column, read from the same
    // function the list reads it from so the two cannot disagree.
    //
    // Reads today's date, which the pure state calculation deliberately never
    // does — a service may, and §7 is explicit that the name is a snapshot
    // taken at the moment of attaching and never revised afterwards.
    //
    // A certificate whose endorsements, extension or thresholds cannot be read
    // falls back to its own expiry date rather than refusing: a document
    // attached under the old naming rule is a far smaller problem than a
    // document that could not be attached.
    QDate nameDateFor(const Certificate& certificate);

    CertificateAttachmentRepository& m_repository;
    CertificateAttachmentStorage&    m_storage;
    VesselAttachmentRoots&           m_roots;
    EndorsementRepository&           m_endorsements;
    CertificateExtensionRepository&  m_extensions;
    AppSettingRepository&            m_appSettings;
    QString                          m_errorString;
};
