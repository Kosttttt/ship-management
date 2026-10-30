# Certificate attachments — implementation spec (step 12)

## §1 Purpose

The certificate module has tracked dates and workflow since step 5, but
never an actual document. This step lets the crew or office attach the
real file (or a whole folder of files) behind a certificate — a scan, a
PDF, whatever they already have — picked from their own computer, copied
into the app's managed data folder, and kept there across renewals and
extensions the same way every other record in this app survives: nothing
is ever silently thrown away.

## §2 What this is, precisely

Two separate things, not one, settled after a design discussion that
corrected an initial instinct to merge them:

**A certificate's own attachment** — the real document behind the
certificate itself. One active attachment per certificate at a time, in a
new `certificate_attachment` table, mirroring `certificate_extension`'s
shape: attaching a new file or folder soft-deletes whatever was active and
inserts a new row, and the *physical* file or folder is moved into an
archive location on disk rather than deleted — a real document is worth
keeping even once replaced, the same reasoning already applied to
extensions. This attachment is **never touched** by starting or resolving
an extension, the same way the certificate's own dates never are (§2 of
`certificate-extension-spec.md`).

**An extension's own document** — the short-term certificate's own
paperwork. This is not a second instance of the mechanism above: it is one
more nullable column, `attachment_path`, directly on `certificate_extension`
itself. An extension row is already a complete, immutable historical
record the moment it exists — nothing ever edits one in place, a new
extension is always a brand-new row — so its file needs no separate
archive step. It simply stays wherever it was put, tied to that row,
forever. Resolving the extension does nothing to this file at all; the
document just sits there in the now-resolved history, exactly where it
always was.

**A certificate can have one file, or a whole folder of files, as its
current attachment — same mechanism either way.** "Attach a file" copies
one file in and renames it to match the certificate; "Attach a folder"
copies an entire folder in and renames *the folder*, but leaves the
original filenames inside it untouched, since renaming several arbitrary
files to one name would just collide or destroy information. Replacing
one with the other works exactly the same way: whatever was there before —
file or folder — gets archived wholesale.

**Naming convention**, confirmed: `<No.>_<Name>_<expiry date>`, e.g.
`15D_Safety_Construction_2027-06-01.pdf` for a file (keeping the original
extension), or `15D_Safety_Construction_2027-06-01` for a folder (no
extension — it's a folder). The certificate's own attachment uses the
certificate's real `expiryDate`; an extension's uses the extension's own
`expiryDate`.

**No file-type restriction.** Any file the user picks is accepted.

**Reachable from two places, both needed**: a standalone action on the
certificate list (for the ordinary case of attaching a scan to a
certificate that doesn't have one yet), and folded into the Renew and
Start-Extension dialogs (so replacing the document happens in the same
motion as the event that usually causes it). Both call the same underlying
mechanism.

## §3 Scope

New files:
- `migrations/008_create_certificate_attachment.sql`
- `migrations/009_add_certificate_extension_attachment_path.sql`
- `core/ManagedFileStore.h/.cpp` — generic, certificate-agnostic file
  operations against the managed data folder. Lives in `core/` on purpose:
  nothing about it is specific to this module, and a future module (the
  eventual Document Management module named in the project's own roadmap)
  will want exactly this.
- `modules/certificates/domain/CertificateAttachment.h` — plain struct.
- `modules/certificates/domain/AttachmentNaming.h/.cpp` — the pure naming
  function (domain layer: no I/O, no widgets, easy to unit test directly).
- `modules/certificates/data/CertificateAttachmentRepository.h/.cpp` —
  SQL only, mirrors `CertificateExtensionRepository`'s shape.
- `modules/certificates/data/CertificateAttachmentStorage.h/.cpp` — this
  module's own file-layout conventions (where a certificate's or an
  extension's files live under `attachments/`), built on top of
  `core/ManagedFileStore`. This is the one place that knows the folder
  layout described in §4; nothing else should construct an `attachments/…`
  path by hand.
- `tests/core/tst_ManagedFileStore.cpp`
- `tests/modules/certificates/tst_CertificateAttachment.cpp`

Changed files:
- `modules/certificates/domain/CertificateExtension.h` — gains
  `QString attachmentPath` (optional; empty means none).
- `modules/certificates/data/CertificateExtensionRepository.cpp` —
  `start()` stores `attachmentPath` the same way it already stores `note`.
- `modules/certificates/ui/CertificateListActions.h/.cpp` — gains the
  standalone Attach action (§7).
- `modules/certificates/ui/CertificateEditDialog.h/.cpp` — gains an
  optional "replace the attached document" control, shown only in renewal
  mode (§8).
- `modules/certificates/ui/CertificateExtensionDialog.h/.cpp` — gains an
  optional "attach a document for this extension" control (§8).
- `CertificatesModule` — threads `CertificateAttachmentRepository` and
  `CertificateAttachmentStorage` through to the widgets/dialogs that need
  them.
- `CMakeLists.txt` and `tests/CMakeLists.txt`.

Out of scope: any UI for browsing a certificate's archived attachment
history (data is preserved; no screen shows it yet, same deferral already
used for extensions); virus-scanning or file-type validation (declined —
"any types its ok"); anything about how these files behave under the
future ship-to-shore sync feature (flagged in `PROJECT-STATUS.md`'s own
"Next step" note as worth thinking about separately — a file is a very
different payload to sync than a database row, and that deserves its own
discussion when sync is actually designed, not a decision made in passing
here).

## §4 File layout under the managed data folder

```
<managed data folder>/attachments/
    <certificateId>/
        15D_Safety_Construction_2027-06-01.pdf      <- current attachment
        archive/
            15D_Safety_Construction_2025-06-01.pdf__archived-20261030T140000Z
        extensions/
            15D_Safety_Construction_ShortTerm_2027-03-01.pdf
```

- The **current** attachment for a certificate sits directly under
  `attachments/<certificateId>/` — deliberately easy to find by eye if
  someone browses the managed folder directly.
- **Archived** attachments move into `attachments/<certificateId>/archive/`,
  with an `__archived-<UTC timestamp>` suffix appended only to guarantee
  uniqueness (two attachments with the same generated name would otherwise
  collide there over a certificate's lifetime). The suffix is mechanical,
  not meant to be read.
- **Extension documents** live in a flat
  `attachments/<certificateId>/extensions/` folder, named the same
  convention with `_ShortTerm_` inserted before the date. No per-extension
  subfolder: the extension's own row is already the archival unit (§2), so
  there is nothing to move once one resolves. On the rare case of a name
  collision (the same certificate extended twice with an identical expiry
  date), `CertificateAttachmentStorage` appends `_2`, `_3`, etc.

**Root resolution — must reuse `Database::defaultFilePath()`, not a fresh
`QStandardPaths` call.** The managed data folder is
`QFileInfo(Database::defaultFilePath()).absolutePath()`; `attachments/`
is a sibling of the database file inside that same folder.
`core/ManagedFileStore::rootPath()` must derive from that exact call —
asking `QStandardPaths` independently risks exactly the step-7 incident
(two different resolutions of "the same" folder on this machine). This is
the single most important correctness requirement in this whole spec;
get this one thing right and the folder-copy-between-machines guarantee
in `CLAUDE.md` §6.9 holds for free.

## §5 `core/ManagedFileStore`

Generic, no knowledge of certificates. No Qt widgets.

```cpp
// core/ManagedFileStore.h
#pragma once

#include <QString>

// File operations against the managed data folder (CLAUDE.md §6.9): the
// folder the database file itself lives in, which is copied whole between
// ship and office, and between machines. Every path this class deals with
// on its public surface is relative to that folder; it resolves to an
// absolute path only internally, and only by asking Database for the
// folder it already uses — never by calling QStandardPaths a second time.
//
// Deliberately certificate-agnostic: any future module that needs to store
// a file (the Document Management module on this project's own roadmap)
// should use this directly rather than reinventing it.
class ManagedFileStore
{
public:
    // The managed data folder itself, as an absolute path. Derived from
    // Database::defaultFilePath(), never from a second QStandardPaths call.
    static QString rootPath();

    // Copies a file or an entire directory (recursively) from sourcePath
    // (anywhere on disk) to <rootPath()>/<destinationRelativeDir>/<name>,
    // where name is desiredName with a numeric suffix appended only if
    // something already exists at that exact destination. Works
    // identically for a file or a directory — the caller does not need to
    // branch on which one sourcePath is.
    //
    // Returns the path actually used, relative to rootPath(), via
    // *relativePathOut. False and errorString() set on failure (source
    // unreadable, destination unwritable); nothing is left partially
    // copied in that case.
    static bool copyInto(const QString& sourcePath,
                         const QString& destinationRelativeDir,
                         const QString& desiredName,
                         QString*       relativePathOut,
                         QString*       errorString);

    // Moves whatever is at <rootPath()>/<relativePath> into
    // <rootPath()>/<archiveRelativeDir>/<same name>__archived-<UTC
    // timestamp, compact ISO-8601>. Works for a file or a directory.
    static bool moveToArchive(const QString& relativePath,
                              const QString& archiveRelativeDir,
                              QString*       errorString);

    // Resolves a path already relative to rootPath() back to an absolute
    // one, for opening it (QDesktopServices::openUrl handles a file or a
    // directory identically — no branching needed in the caller either).
    static QString resolveAbsolute(const QString& relativePath);
};
```

Test this directly against a `QTemporaryDir` standing in for the managed
folder — it needs no database and no certificate to exercise.

## §6 `modules/certificates/data/CertificateAttachmentStorage`

The one place that knows the `attachments/<certificateId>/…` layout in
§4. Built on `ManagedFileStore`; still no SQL.

```cpp
// modules/certificates/data/CertificateAttachmentStorage.h
#pragma once

#include <QString>

// This module's own file-layout conventions (§4), on top of the generic
// core/ManagedFileStore. Nothing outside this class should construct an
// "attachments/…" path by hand.
class CertificateAttachmentStorage
{
public:
    // Archives whatever is currently at attachments/<certificateId>/
    // (excluding its own archive/ and extensions/ subfolders), then copies
    // sourcePath in as the new current attachment, named per
    // AttachmentNaming. Call order matters: archiving first means the new
    // copy can never collide with what it's replacing.
    //
    // currentRelativePath is empty when the certificate has no attachment
    // yet — archiving is then skipped, not an error.
    bool replaceCurrent(const QString& certificateId,
                        const QString& currentRelativePath, // "" if none
                        const QString& sourcePath,
                        const QString& desiredName, // from AttachmentNaming
                        QString*       newRelativePathOut,
                        QString*       errorString);

    // Copies sourcePath into attachments/<certificateId>/extensions/,
    // named per AttachmentNaming, with a numeric suffix on an actual
    // collision. No archiving: an extension row is its own archival unit
    // (§2), so there is nothing to retire here.
    bool addExtensionDocument(const QString& certificateId,
                              const QString& sourcePath,
                              const QString& desiredName,
                              QString*       newRelativePathOut,
                              QString*       errorString);
};
```

`AttachmentNaming::buildName(const Certificate& certificate, const QDate& expiryDateUsed, bool isShortTerm)`
is the pure function producing the sanitized `15D_Safety_Construction_2027-06-01`
(or `..._ShortTerm_...`) string — no extension; the caller appends one for
a file, or uses it bare for a folder. Sanitize against the Windows-legal
filename character set (the strictest of the three platforms, so safe
everywhere): strip `< > : " / \ | ? *` and control characters, collapse
whitespace to single underscores. This is a pure, easily unit-tested
function — no I/O.

## §7 `CertificateAttachmentRepository`

SQL only, mirrors `CertificateExtensionRepository`.

```cpp
class CertificateAttachmentRepository
{
public:
    CertificateAttachmentRepository(QSqlDatabase& database, const InstallationContext& installation);

    bool findActiveFor(const QString& certificateId, std::optional<CertificateAttachment>* attachment);

    // Soft-deletes whatever is currently active for this certificate (if
    // any) and inserts a new row for relativePath, in one call — unlike
    // extension's separate start()/resolve(), there is no reason to make
    // "replace" two steps here. Scope-checked the same transitive way
    // (certificate-extension-spec §5).
    bool attach(const QString& certificateId, const QString& relativePath, QString* newId = nullptr);

    QString errorString() const;
};
```

**This method never touches the filesystem.** The call sequence a dialog
follows is, in order: (1) `findActiveFor()` to learn the current
`relativePath`, if any; (2) `CertificateAttachmentStorage::replaceCurrent()`
— archives the old file/folder, copies the new one in; (3)
`repository.attach()` with the path `replaceCurrent()` actually used.
Deliberately not one database transaction spanning the filesystem move —
the same bounded-risk reasoning renewal and extensions already use: if
step 3 somehow fails after step 2 succeeded, the result is an orphaned
file copy on disk, not a corrupted database row pointing at nothing. Doing
the filesystem work *before* the database write, rather than after, is
what keeps the failure mode that way round.

## §8 UI

**Standalone, on the certificate list** (`CertificateListActions`): one
more button, "Attachment…", with a small menu rather than its own row of
buttons — `QPushButton::setMenu()` with two actions, "Attach File…" and
"Attach Folder…", using `QFileDialog::getOpenFileName()` /
`getExistingDirectory()` respectively. When the selected certificate
already has one, the menu also offers "Open Current Attachment" at the
top, calling `ManagedFileStore::resolveAbsolute()` then
`QDesktopServices::openUrl()` — this works identically whether the
current attachment is a file or a folder, no branching needed. Disabled
with no selection, like every other selection-driven button here.

**Folded into Renew** (`CertificateEditDialog`, renewal mode only): one
optional control — "Replace attached document" — offering the same two
file/folder choices. Left untouched, nothing changes about the
attachment. If used, run the §7 sequence using the certificate's *new*
expiry date for naming, after the certificate update succeeds. A failure
here follows the same pattern already established for a failed
endorsement-clear in step 10: warn, but still accept — the renewal itself
already succeeded, and trapping the user in the dialog over the
attachment alone would invite a second update of data already written.

**Folded into Start Extension** (`CertificateExtensionDialog`): one
optional control, same two choices, writing into the new
`attachmentPath` field on the `CertificateExtension` being started — no
replace/archive step, per §2.

## §9 Edge cases to test

1. Attaching a file for the first time: the row is created, the file
   appears at `attachments/<id>/<name>.<ext>`, named per convention.
2. Attaching a second file archives the first — soft-deleted row, file
   physically moved under `archive/` with its disambiguating suffix — and
   the new one becomes current.
3. Attaching a folder behaves identically at the mechanism level: the
   whole folder is copied and renamed; files inside keep their own names.
4. Replacing a folder with a file, or a file with a folder, archives
   whatever was there regardless of which kind it was.
5. Opening the current attachment resolves and opens correctly for both a
   file and a folder, with no special-casing needed in the calling code.
6. An extension's `attachmentPath` is completely independent of the
   certificate's own attachment: starting or resolving an extension never
   touches `certificate_attachment`, and replacing the certificate's own
   attachment never touches an active extension's document.
7. VESSEL-mode scope: attaching to another vessel's certificate is
   refused, the same transitive check already used for endorsements and
   extensions.
8. Nothing is ever actually deleted: an archived attachment's row survives
   soft-deleted, and its file survives on disk under `archive/`.
9. A relative path stored for one certificate resolves correctly against
   two different roots standing in for two different machines — the
   concrete proof that `CLAUDE.md` §6.9's "copied between ship and office"
   guarantee actually holds for this feature, not just in principle.

## §10 Explicitly deferred

- Any UI for browsing a certificate's archived attachment history. The
  data and the files both survive specifically so this can be added
  later without a migration or a folder-layout change.
- How these files behave under the eventual ship-to-shore sync feature —
  flagged, not decided; a database row and a file are different payloads
  to keep in sync, and that deserves its own design pass when sync itself
  is designed.
- File-type validation or virus scanning — declined on purpose.
- A way to remove an attachment entirely, leaving a certificate with none
  after having had one. Every path built here is "replace," never "clear
  and leave empty"; add this only if it turns out to be a real need.
