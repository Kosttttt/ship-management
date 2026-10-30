#pragma once

#include <QString>

// This module's own file-layout conventions (certificate-attachment-spec §4),
// built on top of the generic core/ManagedFileStore.
//
// Nothing outside this class should construct an "attachments/..." path by
// hand. That is the whole reason it exists: the layout is a convention, and a
// convention spread across three call sites is a convention that will drift.
//
// Still no SQL — recording which path ended up being used is
// CertificateAttachmentRepository's job, and the order the two are called in is
// set out in §7.
class CertificateAttachmentStorage
{
public:
    // Archives whatever is currently attached to this certificate, then copies
    // sourcePath in as the new current attachment, named desiredName.
    //
    // The call order matters: archiving first means the new copy can never
    // collide with the one it is replacing, so the clean generated name is
    // free to be reused.
    //
    // currentRelativePath is empty when the certificate has no attachment yet;
    // archiving is then skipped, which is the ordinary first-attachment case
    // and not an error. Works the same whether either side is a file or a
    // folder — replacing a folder with a file archives the folder wholesale.
    bool replaceCurrent(const QString& certificateId,
                        const QString& currentRelativePath, // "" if none
                        const QString& sourcePath,
                        const QString& desiredName, // from AttachmentNaming
                        QString*       newRelativePathOut,
                        QString*       errorString);

    // Copies sourcePath into this certificate's extensions/ folder, named
    // desiredName, with a numeric suffix added only on an actual collision
    // (the same certificate extended twice to the same expiry date).
    //
    // No archiving: an extension row is its own archival unit (§2), so there is
    // nothing here to retire.
    bool addExtensionDocument(const QString& certificateId,
                              const QString& sourcePath,
                              const QString& desiredName,
                              QString*       newRelativePathOut,
                              QString*       errorString);
};
