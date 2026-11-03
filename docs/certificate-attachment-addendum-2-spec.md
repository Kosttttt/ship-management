# Certificate attachment addendum 2 — flattened storage, archive fix, delete actions, endorsement edit/delete, nearest-due-date naming

## 1. Purpose

Triggered by the developer actually using the per-vessel attachment
addendum (addendum 1) for the first time. Six of seven manual test steps
passed cleanly; this spec covers the issues that surfaced, plus two
related gaps raised in the same discussion (endorsement edit/delete, and
the attachment filename not reflecting an upcoming survey).

## 2. Bug fix: archived filename loses its extension

`ManagedFileStore::moveToArchive()` currently appends
`__archived-<timestamp>Z` **after** the original filename including its
extension, so `15D_Load_Line_2031-02-04.pdf` becomes
`...2031-02-04.pdf__archived-20261102T111954Z` — no longer recognizable
as a PDF to Windows/macOS/Linux, so it can't be opened directly from a
file browser.

**Fix:** insert the marker **before** the extension instead.
`15D_Load_Line_2031-02-04.pdf` archived becomes
`15D_Load_Line_2031-02-04__archived-20261102T111954Z.pdf`. For a folder
attachment (no extension to preserve), behaviour is unchanged — the
marker is simply appended to the folder name, as today.

Implementation: split the relative filename at its last `.` (if any)
before appending the suffix, rather than appending to the whole string.
A folder (no `.` in its own name, or more precisely: the whole input is
a directory, which `moveToArchive()` already knows from its caller)
keeps today's suffix-at-the-end behaviour.

## 3. Flattened storage layout

### 3.1 What changes

Today (addendum 1): `<vessel folder>/attachments/<certificateId>/`,
with `.../archive/` and `.../extensions/` subfolders underneath.

**New:** everything lives directly in the vessel's configured folder.
No `attachments/` wrapper, no per-certificate subfolder, no
`extensions/` subfolder. One exception: a single `archive/` subfolder,
directly under the vessel folder, shared by every certificate on that
vessel — see §3.2.

- A certificate's **current** attachment: `<vessel folder>/<No.>_<Name>_<date>.<ext>`
  (or the folder itself, for a folder attachment) — exactly the
  existing `AttachmentNaming::buildName()` output, just with nothing
  above it in the path.
- An extension's own document: same folder, distinguished by the
  existing `_ShortTerm_` marker already in its generated name — no
  `extensions/` subfolder needed, since the name itself already keeps
  it apart from the certificate's own current attachment.
- A **replaced** (archived) file: `<vessel folder>/archive/<same name,
  with the §2 fix applied>`.

Collision handling needs no change: `ManagedFileStore::copyInto()`
already appends a numeric suffix before the extension on an actual name
collision in the destination folder, and that's the only protection
this ever had even under the per-certificate-id layout.

### 3.2 The shared `archive/` folder

One `archive/` subfolder per vessel, not one per certificate — simpler,
and consistent with "no additional subfolders" beyond this single,
necessary one. Files from every certificate on that vessel land here
when replaced, told apart from each other purely by their own
`<No.>_<Name>_<date>` prefix (already globally meaningful within one
vessel) plus the archive timestamp.

### 3.3 The vessel-folder marker file

Removing the `attachments/` subfolder removes the thing
`VesselAttachmentRoots::changeRoot()` currently checks for to refuse
pointing a second vessel at a folder another vessel is already using
(§2.5 of addendum 1's spec, "a folder already holding attachments is
refused"). Replacement: write a small hidden file,
`.ship-management-vessel`, containing just that vessel's id, into the
folder the first time it's configured (and left alone, not rewritten,
on every subsequent open of the app — only `changeRoot()` ever touches
it). `changeRoot()`'s validation step reads this file if present: if it
names a *different* vessel's id, refuse exactly as before; if it names
*this* vessel's id (re-choosing the same folder) or the file doesn't
exist (a genuinely fresh folder), proceed as today.

## 4. New action: Delete Certificate

A new destructive action, reachable from `CertificateListActions`
alongside Renew and the extension button.

- Confirmation dialog first, explicit that the certificate **and its
  attached files** will be permanently removed — not just the database
  record. No undo from inside the app.
- On confirm:
  1. Soft-delete the certificate row itself.
  2. Soft-delete every endorsement belonging to it.
  3. Soft-delete its active extension, if any.
  4. Delete the real file (or folder) at its current attachment's
     path, if any — a genuine filesystem delete, not an archive move.
  5. Delete the real file at its extension's `attachment_path`, if any
     — same, a genuine delete.
- **Known, accepted limitation** (see §1): does not attempt to clean up
  any older, already-archived copies of this certificate's documents
  sitting in `archive/` — there's no reliable way to map an archived
  file back to the certificate it came from once it's been renamed into
  that shared folder. Only the files the database still has a live
  path for are removed.
- Ordering: database writes happen before filesystem deletes, the
  opposite of attach's "filesystem first" rule — here, a failed delete
  of the *row* must not have already destroyed the file; a failed
  delete of the *file* after the row is gone is logged and surfaced,
  but the certificate stays deleted (consistent with how a failed
  endorsement-clear on renewal already doesn't block the renewal
  itself).

## 5. New action: Delete Attachment

A fourth entry in the Attachment button's menu
(`CertificateListActions`), alongside Open Current Attachment, Attach
File, and Attach Folder — shown only when something is actually
attached, same rule as "Open Current Attachment" today.

- Confirmation dialog (lighter than certificate deletion, but still a
  real confirm — this is irreversible).
- Soft-deletes the active `certificate_attachment` row.
- Deletes the real file genuinely — **no** archive copy. This is
  explicitly different from an ordinary replace, which still archives
  the superseded file: replace is a real supersession worth keeping a
  trail of, delete-attachment exists specifically for correcting a
  mistake, where there's nothing worth keeping.
- After this, the Document column goes back to empty for that row, and
  "Open Current Attachment" stops being offered, exactly as if nothing
  had ever been attached.

## 6. Endorsement edit and delete

Today, `EndorsementEditDialog` is add-only. Adding:

- **Edit**: reuses the same dialog, pre-filled with the selected
  endorsement's existing fields, saving back to the same row (an
  ordinary `UPDATE`, not a new row) — a data-entry correction, not a
  new historical event.
- **Delete**: soft-deletes the row. It disappears from the
  endorsements table shown in the certificate dialog and from
  `computeCertificateState()`'s anniversary matching, the same as every
  other soft-deleted row elsewhere in the app.
- Reached via a right-click/selection on the endorsement row inside
  `CertificateEditDialog`'s endorsements section — exact UI mechanism
  (context menu vs. two buttons) left to implementation, consistent
  with how small a surface this section already is.

## 7. Attachment filename: nearest due date, not always expiry

Today, `AttachmentNaming::buildName()` always uses the certificate's
expiry date. Changing to: whichever of (expiry date, the next annual
survey window's open date, the next intermediate survey window's open
date) is soonest — so the name reflects what actually needs attention
next, not just when the certificate itself runs out.

> **Correction — see `certificate-survey-deadline-fix.md`.** Where the
> paragraph above says a survey window's *open* date, read its **close**
> date. A survey window is named after the day it closes, which is the
> deadline, whether or not the window has opened yet; its opening is only
> a heads-up. The rest of this section is unaffected — in particular the
> first bullet below still holds exactly as written, because the fix was
> made in `computeCertificateState()`'s `dueDate`, so this section's
> single-source-of-truth reading of that field is what carried the
> correction through. Nothing renames a file already on disk, per the
> last bullet.

- Reuses whatever date `computeCertificateState()` already derives for
  `daysLeft` — the single source of truth the list's own Days Left
  column already depends on — rather than a second, independent
  calculation that could drift from what the screen shows.
- For a certificate with no survey requirement at all: unchanged,
  still just the expiry date (there's nothing sooner to compare it to).
- While a short-term extension is active: uses the extension's own
  expiry date, consistent with how the Expiry column itself already
  substitutes it during that window.
- Computed once, at the moment the file is attached — not re-evaluated
  or auto-renamed later as the survey schedule advances. A later
  edit/renewal/new endorsement does not retroactively rename an
  already-attached file.

## 8. Scope

Files expected to change (confirmed exactly once implementation
starts, not guessed here):

- `core/ManagedFileStore` — the §2 archive-naming fix; `moveToArchive()`
  simplified since there's no longer a per-certificate archive path to
  construct, just the shared vessel-level `archive/` folder.
- `core/VesselAttachmentRoots` — the marker-file check in `changeRoot()`
  (§3.3).
- `modules/certificates/data/CertificateAttachmentStorage` — the
  flattened path-building convention (§3.1), and a new delete path for
  §5.
- `modules/certificates/service/CertificateAttachmentService` — a new
  `deleteCurrent()` (§5) alongside the existing `replaceCurrent()`.
- `modules/certificates/domain/AttachmentNaming` — the nearest-due-date
  change (§7), needs the relevant `CertificateState` fields passed in
  rather than just the certificate.
- `modules/certificates/data/CertificateRepository`,
  `EndorsementRepository`, `CertificateExtensionRepository` — new
  delete-cascade methods for §4, and edit/delete for §6.
- `modules/certificates/ui/CertificateListActions` — the new Delete
  Certificate action, the new Delete Attachment menu entry.
- `modules/certificates/ui/EndorsementEditDialog` /
  `CertificateEditDialog`'s endorsements section — edit/delete UI (§6).
- Test suites across all of the above, plus whatever new ones the
  delete actions need of their own.

## 9. Edge cases to test

1. Replacing an attachment still archives the old one, now flat under
   `archive/`, openable (the §2 fix) directly from a file browser.
2. Deleting an attachment removes the real file with no archive copy,
   and the Document column/Open-Current-Attachment both reflect
   "nothing attached" immediately after.
3. Deleting a certificate removes its endorsements, active extension,
   and current attachment/extension-document files; its row and the
   soft-deleted children still exist in the database (confirmed by a
   direct query, not just by the UI hiding them).
4. Deleting a certificate with no attachment at all — no filesystem
   step attempted, no error.
5. A folder already holding a different vessel's marker file is
   refused by `changeRoot()`; the same vessel re-choosing its own
   current folder is accepted and a no-op.
6. A certificate requiring both annual and intermediate surveys: the
   attached filename uses whichever window is actually sooner, checked
   both ways (annual sooner, then intermediate sooner, on two separate
   certificates).
7. A certificate with an active short-term extension: the attached
   filename uses the extension's own expiry, not the certificate's.
8. Editing an endorsement updates the existing row in place — no new
   row appears, the certificate's row count in `endorsement` is
   unchanged.
9. Deleting an endorsement removes it from the dialog's list and from
   the next `computeCertificateState()` evaluation, without touching
   any other endorsement on the same certificate.

## 10. Explicitly out of scope

- Sweeping `archive/` for a deleted certificate's older copies (§4) —
  not attempted, flagged as a known limitation rather than solved.
- Any change to how the extension's own document is added or replaced
  beyond its new flat storage location — it still has no "replace"
  flow of its own; this addendum doesn't add one.
- Undo for any of the new delete actions.
