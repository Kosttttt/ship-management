#pragma once

#include <QString>

// Recursive filesystem primitives: copy a thing, remove a thing, and the
// "_2 before the extension" naming rule they share.
//
// Split out of ManagedFileStore.cpp, which had grown past the ~300-line
// guideline in CLAUDE.md §9. The seam is a real one rather than a line count
// being satisfied: nothing here knows what a managed root is, what a vessel is
// or what an attachment is. These are the operations Qt does not provide —
// QFile::copy() refuses a directory and QDir::rename() refuses to cross a
// volume — and ManagedFileStore above is the policy that decides where to
// point them.
//
// Free functions in a namespace rather than a class, for the same reason
// AttachmentNaming is: there is no state to hold.
namespace FileTree {

// "report.pdf" with index 3 becomes "report_3.pdf", not "report.pdf_3": the
// extension has to stay last or the operating system stops recognising the
// file. A name with no extension — a folder, usually — simply gains the
// suffix. An index of 1 or less returns the name unchanged, which is what
// makes "only suffix on an actual collision" a one-liner at the call sites.
QString nameWithIndex(const QString& desiredName, int index);

// Inserts marker immediately before the extension, or at the end when there
// is none: "scan.pdf" with "__archived-X" becomes "scan__archived-X.pdf".
//
// certificate-attachment-addendum-2-spec §2. The bug this fixes was appending
// the marker to the whole filename, which left the archived copy with no
// recognisable extension and therefore nothing on Windows, macOS or Linux
// willing to open it.
//
// treatAsFolder forces the append-at-the-end behaviour: a folder called
// "survey.pack" has no extension to preserve, and splitting at its dot would
// bury the marker in the middle of its name.
QString nameWithMarker(const QString& fileName, const QString& marker, bool treatAsFolder);

// Deletes a file or a whole directory. A path that does not exist is not an
// error — there is nothing to do and nothing was left behind, which is what
// every caller actually wants to know.
bool removeAnything(const QString& absolutePath);

// Copies a file or an entire directory recursively. Symbolic links are not
// followed: one pointing back at an ancestor would otherwise recurse until the
// disk filled.
//
// A failure part way leaves whatever it had already written; the caller is
// responsible for taking the partial destination back out again, because only
// the caller knows whether the destination was its own to begin with.
bool copyRecursively(const QString& source, const QString& destination, QString* errorString);

// Whether one path is the same as, or sits inside, another. Both are
// canonicalised first, so two spellings of one folder are recognised as one
// folder.
//
// Used to refuse moving a folder into itself, which would otherwise consume
// its own source as it went.
bool isSameOrInside(const QString& path, const QString& possibleAncestor);

} // namespace FileTree
