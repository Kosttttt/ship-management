#pragma once

#include <QString>
#include <QStringList>

// File operations against a managed file root: copy something in under a
// generated name, retire what it replaced, delete something outright, resolve
// a stored relative path back to an absolute one, and move a whole tree — or a
// folder's whole contents — from one root to another.
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
    // exists at that exact destination. An empty destinationRelativeDir means
    // the root itself, which is where a vessel's documents now live
    // (certificate-attachment-addendum-2-spec §3.1). Works identically for a
    // file or a directory — the caller does not branch on which one sourcePath
    // is.
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
    // <root>/<archiveRelativeDir>/, with "__archived-<UTC timestamp, compact
    // ISO-8601>" inserted immediately before the extension. Works for a file
    // or a directory; a directory, having no extension to preserve, simply
    // gains the marker at the end of its name.
    //
    // The marker exists only to guarantee uniqueness — two attachments
    // generated from the same certificate name and date would otherwise
    // collide in the archive over that certificate's lifetime, and since
    // certificate-attachment-addendum-2-spec §3.2 the archive is shared by
    // every certificate on the vessel, so collisions are likelier than they
    // were. It is mechanical, not meant to be read.
    //
    // It goes *before* the extension rather than after it (§2): appended to
    // the whole filename it left the archived copy looking like a file of no
    // known type, which nothing would open from a file browser.
    static bool moveToArchive(const QString& root,
                              const QString& relativePath,
                              const QString& archiveRelativeDir,
                              QString*       errorString);

    // Deletes whatever is at <root>/<relativePath>, for good — no archive copy
    // (certificate-attachment-addendum-2-spec §4 and §5). Works for a file or
    // a folder.
    //
    // Guarded in two ways that matter more here than anywhere else in this
    // class, because this is the one operation that destroys something. An
    // empty relativePath is refused rather than resolving to the root, and an
    // absolute or escaping relativePath is refused rather than being joined
    // (QDir::filePath() hands an absolute path straight back, so "nothing
    // attached" or a stray "../.." would otherwise name the user's own
    // folders). CLAUDE.md §10.1 is the reason these are checks and not
    // assumptions.
    //
    // Something that is already gone is success: the caller wanted it not to
    // be there, and it is not.
    static bool deleteAt(const QString& root, const QString& relativePath, QString* errorString);

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

    // The names of everything directly inside a folder, hidden entries
    // included, or an empty list when the folder does not exist. Names only,
    // not paths, so the same list can be used against either side of a move.
    //
    // Hidden entries are listed because a document can perfectly well be one;
    // which of them a caller then chooses to move is the caller's business.
    static QStringList entriesIn(const QString& directory);

    // Moves the named entries from sourceDir to targetDir, leaving anything
    // else in either folder alone.
    //
    // This is what §2.5's "a vessel's folder change takes its documents with
    // it" became once certificate-attachment-addendum-2-spec §3.1 flattened
    // the layout: there is no longer one "attachments" subtree to move, so the
    // thing to move is the folder's own entries. A name that is no longer
    // there is skipped rather than failing — the caller listed it a moment
    // ago, and something it has itself excluded is its business.
    //
    // Named rather than "everything in sourceDir" so that undoing a move
    // touches exactly what the move touched. The destination may have held the
    // user's own unrelated files before any of this, and putting those into
    // the vessel's old folder on a rollback would be a second bug dressed as
    // a recovery.
    //
    // Every name is checked against the destination first and the whole move
    // is refused if any of them is already taken, so two vessels' documents
    // can never be quietly pooled. An entry that fails part way through has
    // the entries already moved put back, so a failure leaves the vessel
    // exactly where it was.
    static bool moveNamedEntries(const QString&     sourceDir,
                                 const QString&     targetDir,
                                 const QStringList& names,
                                 QString*           errorString);
};
