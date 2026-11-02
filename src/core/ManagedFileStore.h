#pragma once

#include <QString>

// File operations against a managed file root: copy something in under a
// generated name, retire what it replaced, resolve a stored relative path back
// to an absolute one, and move a whole tree from one root to another.
//
// Every path this class hands back or takes in, other than the root itself and
// a source somewhere on the user's own disk, is relative to that root. Storing
// relative paths is what lets a folder of documents be copied between ship and
// office and still be found (CLAUDE.md §6.9).
//
// The root is a parameter rather than something this class decides. It used to
// decide: until the attachment addendum there was one managed folder for the
// whole installation — the folder the database file lives in — and rootPath()
// below is still exactly that folder. Each vessel now points its own documents
// at a folder of its own choosing instead
// (certificate-attachment-addendum-spec §2.1), and which folder belongs to
// which vessel is a question about vessels, so it is answered by
// VesselAttachmentRoots and never guessed at here.
//
// rootPath() survives because two things still need the installation's own
// folder: resolving an attachment made before the addendum shipped (§2.2), and
// the throwaway data directory CLAUDE.md §10.1 requires.
//
// Deliberately certificate-agnostic, and vessel-agnostic too. Any future
// module that needs to store a file — the Document Management module on this
// project's roadmap — should use this directly rather than reinventing it,
// which is why it lives in core/ and not in a module.
class ManagedFileStore
{
public:
    // The installation's own data folder: the folder the database file is in.
    //
    // Derived from Database::defaultFilePath(), never from a second
    // QStandardPaths call. That is not a style preference: on the machine this
    // project is developed on, two resolutions of "the per-user data folder"
    // are genuinely different files with the same-looking name, which is
    // exactly how the step-7 incident recorded in CLAUDE.md §10.1 destroyed a
    // real database.
    //
    // No longer where attachments go. It is the pre-addendum location, which
    // §2.2 still reads so that an attachment made before the change can be
    // opened.
    static QString rootPath();

    // Points rootPath() somewhere else until it is cleared again with an empty
    // string. Two callers need this and neither is a shortcut:
    //
    //   * a test standing a QTemporaryDir in for the installation's folder,
    //     which is the only way to exercise this class, and the vessel
    //     resolution built on it, without writing into the real one;
    //   * the throwaway data directory CLAUDE.md §10.1 requires a manual
    //     visual check to use instead of the live folder.
    //
    // Process-wide state, so a test sets it in its fixture and clears it in
    // the fixture's destructor rather than leaving it set for the next test.
    static void setRootPathOverride(const QString& absolutePath);

    // The override as it stands, or an empty string when none is set. Exposed
    // so that VesselAttachmentRoots can honour it ahead of any vessel lookup
    // (certificate-attachment-addendum-spec §2.4) without owning a second copy
    // of this state.
    static QString rootPathOverride();

    // Copies a file or an entire directory (recursively) from sourcePath
    // (anywhere on disk) to <root>/<destinationRelativeDir>/<name>, where name
    // is desiredName with a numeric suffix added only if something already
    // exists at that exact destination. Works identically for a file or a
    // directory — the caller does not branch on which one sourcePath is.
    //
    // Returns the path actually used, relative to root, via *relativePathOut.
    // False and *errorString on failure (no root, source unreadable,
    // destination unwritable); nothing is left partially copied in that case.
    static bool copyInto(const QString& root,
                         const QString& sourcePath,
                         const QString& destinationRelativeDir,
                         const QString& desiredName,
                         QString*       relativePathOut,
                         QString*       errorString);

    // Moves whatever is at <root>/<relativePath> into
    // <root>/<archiveRelativeDir>/<same name>__archived-<UTC timestamp,
    // compact ISO-8601>. Works for a file or a directory. The suffix exists
    // only to guarantee uniqueness — two attachments generated from the same
    // certificate name and expiry date would otherwise collide in the archive
    // over that certificate's lifetime. It is mechanical, not meant to be read.
    static bool moveToArchive(const QString& root,
                              const QString& relativePath,
                              const QString& archiveRelativeDir,
                              QString*       errorString);

    // Resolves a path already relative to root back to an absolute one, for
    // opening it. An empty relativePath gives an empty string rather than the
    // root folder itself: "there is no attachment" must never open the whole
    // folder by accident. An empty root does the same.
    static QString resolveAbsolute(const QString& root, const QString& relativePath);

    // Moves a whole directory tree from one absolute place to another, for
    // certificate-attachment-addendum-spec §2.5: a vessel's folder changing
    // takes the vessel's files with it.
    //
    // A rename first, which is fast and near-atomic when both sides are on one
    // volume; a recursive copy followed by deleting the source when they are
    // not, because QDir::rename() simply fails across volumes rather than
    // doing the copy itself.
    //
    // Either the tree is at targetAbsolutePath when this returns true, or it
    // is still wholly at sourceAbsolutePath and this returned false. A copy
    // that fails part way takes its own partial destination back out again, so
    // there is never a half tree for a stored path to point at.
    //
    // One exception, deliberate: if the copy succeeded and only the deletion
    // of the source failed, this reports success. The files are complete at
    // the destination, which is what the caller is about to record; reporting
    // failure would leave a full copy at both ends *and* make the next attempt
    // impossible, because the destination would then already hold a tree.
    static bool moveTree(const QString& sourceAbsolutePath,
                         const QString& targetAbsolutePath,
                         QString*       errorString);
};
