#pragma once

#include <QString>

// File operations against the managed data folder (CLAUDE.md §6.9): the folder
// the database file itself lives in, which is copied whole between ship and
// office, and between machines.
//
// Every path on this class's public surface is relative to that folder. It
// resolves to an absolute path only internally, and only by asking Database
// for the folder it already uses — never by calling QStandardPaths a second
// time. That is not a style preference: on this machine the two resolutions
// are genuinely different files with the same-looking name, which is exactly
// how the step-7 incident recorded in CLAUDE.md §10.1 destroyed a real
// database.
//
// Deliberately certificate-agnostic. Any future module that needs to store a
// file — the Document Management module on this project's own roadmap — should
// use this directly rather than reinventing it, which is why it lives in core/
// and not in the certificates module.
class ManagedFileStore
{
public:
    // The managed data folder itself, as an absolute path. Derived from
    // Database::defaultFilePath(), never from a second QStandardPaths call.
    static QString rootPath();

    // Points rootPath() somewhere else until it is cleared again with an empty
    // string. Two callers need this and neither is a shortcut:
    //
    //   * a test standing a QTemporaryDir in for the managed folder, which is
    //     the only way to exercise this class without writing into the real
    //     one (certificate-attachment-spec §5);
    //   * the throwaway data directory CLAUDE.md §10.1 requires a manual
    //     visual check to use instead of the live folder.
    //
    // Process-wide state, so a test sets it in its fixture and clears it in
    // the fixture's destructor rather than leaving it set for the next test.
    static void setRootPathOverride(const QString& absolutePath);

    // Copies a file or an entire directory (recursively) from sourcePath
    // (anywhere on disk) to <rootPath()>/<destinationRelativeDir>/<name>,
    // where name is desiredName with a numeric suffix added only if something
    // already exists at that exact destination. Works identically for a file
    // or a directory — the caller does not branch on which one sourcePath is.
    //
    // Returns the path actually used, relative to rootPath(), via
    // *relativePathOut. False and *errorString on failure (source unreadable,
    // destination unwritable); nothing is left partially copied in that case.
    static bool copyInto(const QString& sourcePath,
                         const QString& destinationRelativeDir,
                         const QString& desiredName,
                         QString*       relativePathOut,
                         QString*       errorString);

    // Moves whatever is at <rootPath()>/<relativePath> into
    // <rootPath()>/<archiveRelativeDir>/<same name>__archived-<UTC timestamp,
    // compact ISO-8601>. Works for a file or a directory. The suffix exists
    // only to guarantee uniqueness — two attachments generated from the same
    // certificate name and expiry date would otherwise collide in the archive
    // over that certificate's lifetime. It is mechanical, not meant to be read.
    static bool moveToArchive(const QString& relativePath,
                              const QString& archiveRelativeDir,
                              QString*       errorString);

    // Resolves a path already relative to rootPath() back to an absolute one,
    // for opening it. An empty relativePath gives an empty string rather than
    // the root folder itself: "there is no attachment" must never open the
    // whole data folder by accident.
    static QString resolveAbsolute(const QString& relativePath);
};
