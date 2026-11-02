# Certificate attachment addendum — per-vessel storage, Add/Edit control, list indicators

## 1. Purpose

Four refinements requested after manually testing step 12 against the real
app (`dist\ShipManagement\ShipManagement.exe`), before step 13 begins. Not a
numbered build-order step — an addendum to step 12, the same way the step 5
addendum handled feedback after certificate CRUD first shipped.

The developer's framing of the module, which motivates §2 in particular:
certificates and their real paperwork arrive at the office (or are endorsed
there), get entered or updated in the program, and the program's job is to
say when something needs acting on — arranging a survey or a service. The
same tool is meant to go to vessels later too, for cross-checking, and is
meant to work as a real storage/observation instrument for the documents
themselves, not just an alerting layer on top of dates typed into a form.
That is why where the actual files live matters enough to be configurable
per vessel, rather than being an implementation detail the app hides.

## 2. Per-vessel attachment storage root

### 2.1 What changes

Today every certificate's attachment lives under one single folder — the
folder the database file is in (`ManagedFileStore::rootPath()`), by way of
`Database::defaultFilePath()`. This addendum replaces that with **one
folder per vessel**, chosen by whoever is running the installation:

- **VESSEL mode**: the installation has exactly one vessel, so this is "pick
  the folder for my ship" — one control, no list.
- **OFFICE mode**: the installation manages a fleet, so each vessel gets its
  own independently-chosen folder.

**The database file does not move.** It stays in the app's own data folder
exactly as today. Only attachments — a vessel's own real-world documents —
move to wherever that vessel's folder is pointed at. This is a deliberate
scope line: the database is this installation's bookkeeping; a vessel's
documents are the vessel's own files, which is exactly why the developer
wants them somewhere they already use, possibly a folder that syncs itself
(a Google Drive Desktop folder, for instance — the app needs no awareness
of this; a synced folder is indistinguishable from an ordinary one on disk).

### 2.2 Hard requirement, no silent fallback

Once this ships, attaching anything to a certificate — Attach File, Attach
Folder, the Add/Edit dialog's control from §3, the renewal and extension
dialogs' controls — is **refused with a friendly message** ("this vessel's
attachment folder has not been set yet — choose one in Settings") whenever
that certificate's vessel has no folder configured. There is no silent
fallback to the old global location. **Opening** an already-stored
attachment is not affected by this — a certificate attached before this
addendum shipped can still be opened; only new attach/replace actions are
blocked until a folder exists.

### 2.3 Where the path lives

A new column on `vessel` (core-owned, since this concept belongs to the
vessel itself, not to the certificates module — any future module that
stores files against a vessel, the Document Management module among them,
gets this for free rather than inventing its own):

```sql
ALTER TABLE vessel ADD COLUMN attachment_root_path TEXT;
```

Nullable, meaning "not yet configured." Absolute, not relative — unlike
every path *stored against a certificate*, this one describes where the
vessel's own root is, on this machine, right now; it is read once per
install/reconfiguration, not copied between machines the way the managed
folder used to be.

### 2.4 `ManagedFileStore` becomes vessel-aware

`rootPath()` (no arguments, derived from `Database::defaultFilePath()`) is
replaced for attachment purposes by something that resolves a specific
vessel's root, e.g.:

```cpp
// Returns the vessel's configured folder, or an empty string if none is
// set yet — callers treat empty as "refuse with a friendly message",
// never as "fall back to somewhere else."
static QString rootPathForVessel(const QString& vesselId, QString* errorString);
```

`setRootPathOverride()` stays exactly as it is for tests — it is checked
first, before any vessel lookup, same precedence as today.

`CertificateAttachmentStorage` and `CertificateAttachmentService` need the
vessel id threaded through. `Certificate` already carries `vesselId`, so no
new parameter is needed at most call sites — `certificate.vesselId` is
already in hand everywhere a certificate is. `CertificateExtension` does
not carry a vessel id directly, so `addExtensionDocument()`'s existing
`const Certificate&` parameter (not just the extension) is what supplies it
— already the shape the current signature uses, so this falls out for free.

### 2.5 Changing a vessel's folder later

Explicitly wanted, not just tolerated: changing an already-configured
vessel's folder **moves its existing attachments to the new location**,
rather than leaving them behind or requiring a manual copy.

Sequence, in the failure direction this project always prefers (never leave
the database claiming something that isn't physically true):

1. Validate the newly-chosen folder: it must exist and be writable, and
   must not already contain an `attachments\` folder of its own (refuse
   with a clear message rather than silently merging two vessels' files
   into one folder by accident).
2. Move the vessel's entire `attachments\` subtree from the old root to the
   new one. Prefer a rename when both are on the same volume (fast,
   atomic-ish); fall back to a recursive copy followed by deleting the
   source when they are not (a plain `QDir::rename()` fails silently
   across volumes on some platforms, so this has to be checked for, not
   assumed to just work).
3. Only after the move succeeds, update `vessel.attachment_root_path` to
   the new folder. If the move fails partway, nothing about the stored
   path changes, and the old root is left exactly as it was — a failed
   move must never cause the app to start looking for files via a path
   that doesn't actually have them.

### 2.6 Explicitly out of scope here

**No automatic migration of the single attachment that already exists**
from manual testing (`14_aaa_2031-10-29.pdf`, created before this addendum,
under the old global location) — confirmed with the developer: that
predates per-vessel folders entirely and is test data, not something worth
a one-time migration path for. Once a vessel's folder is configured, it
can simply be re-attached through the ordinary UI if it's still wanted.

**No Google Drive (or any cloud provider) integration.** The folder chooser
is the same `QFileDialog::getExistingDirectory()` mechanism Attach Folder
already uses (`AttachmentChooser::chooseFolder()`), reused as-is or via a
thin, differently-worded wrapper. Whether the chosen folder happens to sync
itself is entirely outside the app's knowledge or concern.

**`CLAUDE.md` §6.9 needs a wording update once this is built** — "the
managed data folder" stops being one single folder and becomes "the
relevant vessel's configured folder." Flag this for a follow-up `CLAUDE.md`
fix the same way the `certificate_attachment` naming correction was folded
in after step 12's own build.

## 3. Attach control in the ordinary Add/Edit Certificate dialog

Today `CertificateEditDialog`'s attachment section (`buildAttachmentSection()`,
an `AttachmentSourceField`) only appears when `isRenewal` is true. This
addendum makes it appear **always** — Add and ordinary Edit, not just
Renewal — since a certificate's real document is just as often already in
hand the first time it's entered as it is at renewal time.

- For **Add** (no existing certificate, no id yet): the attachment, if one
  was chosen, is applied *after* `m_repository.create()` succeeds, using
  the id the repository just generated — the same ordering renewal already
  uses for exactly the same reason (there is nothing to name the stored
  file after, or attach it to, before the row exists).
- For ordinary **Edit** (existing certificate, not a renewal): applied
  after `m_repository.update()` succeeds, using the certificate's *current*
  expiry date (unchanged — this is not a renewal, so there is no new date
  to name the file after).
- The field's empty-state wording should say the right thing per mode:
  Renewal already says "the document already attached stays as it is";
  Edit should say the same (there may already be one); Add should say
  something like "no document is attached yet" (there is nothing to keep).

No other behavioural change to the dialog — the Endorsements section's
existing visibility rule (edit-only, non-renewal) is untouched.

## 4. List column: attachment indicator

A new column in `CertificateListWidget`'s table, appended after the
existing `Days Left` column (so no other column's position or stored
`Qt::UserRole` data needs renumbering beyond what §5 already requires).

- **Content**: a plain icon/glyph when the certificate has a current
  attachment, nothing when it doesn't — no filename, no button chrome. It
  doubles as a quiet "something's attached" indicator at a glance, which is
  the point: the developer specifically wants this column to work as an
  indicator, not only as a shortcut.
- **Interaction**: a single click anywhere in this column's cell opens the
  current attachment directly — no row selection required first, no going
  through the Attachment… menu. Double-click, everywhere including this
  column, keeps opening Edit Certificate exactly as it does today; this is
  why single-click was chosen specifically for this column, since nothing
  else currently reacts to a single click, so there's no ambiguity to
  resolve.
- **Ownership**: this lives in `CertificateListWidget` itself, not
  `CertificateListActions` — it is triggered by *which cell was clicked*,
  not by *what is currently selected*, which is the same distinction that
  already keeps Add Certificate out of `CertificateListActions` today.
- **Data source**: `reload()` already computes everything else per row; it
  gains one more read per certificate — `m_attachments.currentAbsolutePath(certificate.id)`
  — the same accepted N+1-per-certificate pattern already used for
  endorsements and the active extension on this same screen.
- If nothing is attached, the cell is empty and a click on it does nothing
  (not even an error message — there is nothing to report).

## 5. List column: Issue Date

Reinstated, having been removed at step 7 in favour of a "what needs
attention" focus — a reasonable call at the time, not something wrong that
needs correcting, just one the developer wants revisited now.

Inserted immediately after **Name**, before **Status**:

```
No. · Name · Issue Date · Status · Expiry Date · Survey From · Survey To · Days Left · [attachment]
```

So the two dates most relevant to "what is this document and when was it
issued/does it expire" sit next to each other, rather than separated by
status and survey columns. `fillRow()` gains one more cell,
`certificate.issueDate.toString(kDateFormat)` — no new logic, since
`issueDate` is already a plain field on `Certificate` with nothing computed
about it.

## 6. Scope

New:
- `migrations/010_add_vessel_attachment_root_path.sql`

Changed:
- `core/Vessel.h` — gains `attachmentRootPath` (or similar), matching the
  new column.
- `core/VesselRepository.h/.cpp` — reads/writes the new column alongside
  every other vessel field.
- `core/ManagedFileStore.h/.cpp` — gains the vessel-aware resolution from
  §2.4, and the move-on-change logic from §2.5 (likely its own method,
  e.g. `moveVesselRoot()`, rather than folding into `moveToArchive()`,
  since the two operate at different granularities — one file/folder,
  versus an entire vessel's tree).
- `app/SettingsPage.h/.cpp` — gains the per-vessel (OFFICE) or single
  (VESSEL) folder control from §2.1–§2.2.
- `modules/certificates/data/CertificateAttachmentStorage.h/.cpp`,
  `modules/certificates/service/CertificateAttachmentService.h/.cpp` —
  thread the vessel id through to the new `ManagedFileStore` entry point,
  and surface the "no folder configured" refusal with a friendly message.
- `modules/certificates/ui/CertificateEditDialog.h/.cpp` — §3.
- `modules/certificates/ui/CertificateListWidget.h/.cpp` — §4, §5.
- `modules/certificates/ui/CertificateListRow.h` — if the attachment
  indicator's "is attached" fact needs to ride on the row the same way
  the extension expiry already does (needed if clicking relies on reading
  it back from the item rather than re-querying the service on click;
  either is reasonable, left to whichever reads more simply against the
  existing pattern).
- `CMakeLists.txt`, `tests/CMakeLists.txt` — the new migration, and
  whatever new/changed test files this produces.
- `CLAUDE.md` §6.9 — the wording update noted in §2.6, once built.

## 7. Edge cases to test

1. A vessel with no folder configured: Attach File/Folder refused with a
   friendly message, in all four places that offer it (list, Add/Edit,
   Renewal, Extension). Opening an attachment that predates this addendum
   still works.
2. Setting a vessel's folder for the first time: no files move (§2.6 — not
   a "change", nothing existed to move from this vessel's perspective
   under the new model).
3. Changing an already-configured vessel's folder: existing attachments
   physically relocate; the certificate's `relative_path` in the database
   does not change (only the root changes; the relative part is still
   correct under the new root).
4. Changing a vessel's folder to one that already contains an
   `attachments\` folder: refused, nothing moved, nothing changed.
5. VESSEL mode shows one folder control with no list; OFFICE mode shows
   one row per vessel.
6. Add Certificate with a document chosen: the file is attached, named
   from the certificate's own just-saved number/name/expiry, after the
   certificate itself is successfully created.
7. Ordinary Edit (not Renewal) with a document chosen: replaces the
   current attachment using the certificate's *existing* expiry date, not
   a new one.
8. The list's attachment-indicator column: present and clickable only for
   rows with something attached; a single click opens it without the row
   needing to be selected first; double-click anywhere, including this
   column, still opens Edit Certificate.
9. Issue Date column renders correctly and sorts as a date (not as text),
   consistent with every other date column already on this screen.

## 8. Explicitly deferred

- Any UI for moving a vessel's folder onto removable/network media that
  isn't already mounted as a normal path — out of scope; this only ever
  deals in ordinary filesystem paths.
- Any awareness of specific cloud-sync providers (Google Drive or
  otherwise) — see §2.6.
- Migrating the pre-addendum test attachment — see §2.6.
- A "remove attachment entirely" action — unchanged from the original
  attachment spec's §10; still deferred, still not part of this addendum.
