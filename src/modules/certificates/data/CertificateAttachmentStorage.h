#pragma once

#include <QString>

// This module's own file-layout conventions (certificate-attachment-spec §4,
// flattened by certificate-attachment-addendum-2-spec §3.1), built on top of
// the generic core/ManagedFileStore.
//
// The layout is now almost no layout at all:
//
//   <vessel folder>/<No.>_<Name>_<date>.<ext>     the current attachment
//   <vessel folder>/<No.>_<Name>_ShortTerm_<date>.<ext>
//                                                 an extension's own document
//   <vessel folder>/archive/<same name, marked>   what a replacement retired
//
// There is no "attachments" wrapper, no per-certificate subfolder and no
// "extensions" subfolder. Manual testing of addendum 1 made the case plainly:
// a crew member opening the folder in Explorer wants to see the ship's
// certificates, not a tree of UUIDs. An extension's document needs no folder
// of its own because its generated name already carries _ShortTerm_, and a
// certificate's documents need no folder of their own because the generated
// name already starts with the certificate's number and name.
//
// The single remaining subfolder is archive/, shared by every certificate on
// the vessel (§3.2). It is the one place where a name really can repeat — the
// same certificate replaced twice — and the archive marker
// ManagedFileStore::moveToArchive() adds is what keeps those apart.
//
// Every entry point takes the root to work within. It does not look one up:
// which folder a vessel's documents belong in is a question about vessels, and
// is answered once, by the service, before anything here is called (§2.4).
//
// Still no SQL — recording which path ended up being used is
// CertificateAttachmentRepository's job, and the order the two are called in is
// set out in §7.
class CertificateAttachmentStorage
{
public:
    // The one subfolder this module creates inside a vessel's folder (§3.2).
    // Public because the tests assert on it and because a reader of the
    // layout comment above should be able to find the string it refers to.
    static const QString kArchiveSubfolder;

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
    bool replaceCurrent(const QString& root,
                        const QString& certificateId,
                        const QString& currentRelativePath, // "" if none
                        const QString& sourcePath,
                        const QString& desiredName, // from AttachmentNaming
                        QString*       newRelativePathOut,
                        QString*       errorString);

    // Copies sourcePath into the vessel's folder under desiredName, which
    // carries _ShortTerm_ and is what keeps it apart from the certificate's
    // own current attachment (§3.1). A numeric suffix is added only on an
    // actual collision — the same certificate extended twice to the same
    // expiry date.
    //
    // No archiving: an extension row is its own archival unit (§2), so there is
    // nothing here to retire.
    bool addExtensionDocument(const QString& root,
                              const QString& certificateId,
                              const QString& sourcePath,
                              const QString& desiredName,
                              QString*       newRelativePathOut,
                              QString*       errorString);

    // Deletes a stored file or folder outright, with no archive copy
    // (certificate-attachment-addendum-2-spec §4 and §5). A replacement
    // archives what it supersedes because a supersession is worth a trail; a
    // deletion exists to correct a mistake, where there is nothing worth
    // keeping.
    //
    // A thin pass-through to ManagedFileStore::deleteAt(), which holds the
    // guards that matter: an empty or escaping relative path is refused rather
    // than resolved against the root.
    bool deleteStored(const QString& root, const QString& relativePath, QString* errorString);
};
