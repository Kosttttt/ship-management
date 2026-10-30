#pragma once

#include <QDate>
#include <QString>

struct Certificate;

// The domain layer: plain C++ with no widgets, no SQL and no I/O
// (CLAUDE.md §4 rule 1). Both functions here are pure — they look at strings
// and dates and nothing else — which is why they can be unit-tested without a
// database, a window, or a single file on disk.

// How an attached file or folder is named inside the managed data folder
// (certificate-attachment-spec §6).
namespace AttachmentNaming {

// "<No.>_<Name>_<expiry date>", e.g. 15D_Safety_Construction_2027-06-01, with
// _ShortTerm_ inserted before the date for an extension's own document.
//
// No file extension: the caller appends one for a file, or uses the name bare
// for a folder (renaming a folder to something ending in ".pdf" would be a
// lie). expiryDateUsed is the certificate's own expiry date for a certificate's
// attachment, and the extension's own expiry date for an extension document —
// the caller decides, because only the caller knows which one it is naming.
//
// The result is sanitised against the Windows-legal filename character set,
// which is the strictest of the three platforms and therefore safe on all of
// them: the characters < > : " / \ | ? * and control characters are dropped,
// and runs of whitespace collapse to single underscores.
QString buildName(const Certificate& certificate, const QDate& expiryDateUsed, bool isShortTerm);

// Appends the source file's extension to a name from buildName(), so a .pdf
// stays a .pdf once copied in. A folder source gets nothing appended.
//
// sourceIsFolder is passed in rather than discovered, because asking the
// filesystem what a path is would be I/O, and this layer does none.
QString withExtensionFrom(const QString& baseName,
                          const QString& sourcePath,
                          bool           sourceIsFolder);

} // namespace AttachmentNaming
