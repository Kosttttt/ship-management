# Document Management — module specification

Version 1.0. The second module, started after Certificate Control was
closed out (`certificate-control-v1.0`). Module-level decisions first, then
step 1 in implementable detail; steps 2–4 are outlined and get their own
specs when they come up.

---

## 1. What the module is for — decided with the developer

**Documents held** — all four kinds:
- **SMS manuals & procedures** — the company's Safety Management System,
  under ISM document control (one current revision; older ones kept but
  superseded).
- **Circulars & notices** — company circulars, flag and class notices.
- **Plans & manuals** — approved plans, drawings, makers' manuals.
- **Reports & records** — PSC / flag / vetting inspection reports, survey
  and audit reports.

**Purpose:** an organised library with full version history first; document
control on top of it. Old versions go to an archive, never lost.

**Manuals written in the program, with change tracking.** The office edits
SMS manuals inside the program; the program works out what changed between
revisions, and the vessel can **print exactly the changed pages** with one
button to replace them in the printed manual. Export: PDF, and Word via
OpenDocument `.odt` (opens in Word; no extra library). True `.docx` only
later, if `.odt` proves insufficient.

**Office → vessel:** export/import **packages** (a file sent by email or
USB), and the **master confirms receipt for the vessel** (name and date),
the confirmation going back to the office the same way.

**No dates for now** — no expiry or review-due tracking in this module.
Each version may carry a revision date as plain information; nothing is
scheduled or alerted from it.

### Why manuals are written *in* the program (and uploaded PDFs aren't page-printed)

Printing chosen pages of an *uploaded* PDF needs a PDF renderer. Qt's own,
Qt PDF, **cannot be built with MinGW** ("Currently, it is not possible to
build Qt PDF using MinGW" — Qt PDF Platform Notes), and MinGW is this
project's Windows compiler (`CLAUDE.md` §2). The alternatives — switching
to MSVC, or an outside PDF library — are large changes.

A manual written in the program avoids the problem entirely: the program
lays out the pages itself (`QTextDocument` + `QPrinter`, as the certificate
PDF already does), so it knows which pages each changed section falls on
and can print those pages directly. Uploaded files (circulars, plans,
reports, and any manual not yet moved into the editor) are stored with full
version history and **opened in the computer's normal viewer**.

## 2. Build order for this module

1. **Library** — documents (fleet-wide or per vessel), four categories,
   upload Word/PDF/any file, every version kept, older ones superseded.
   *This document, §4.*
2. **Manual editor** — manuals as chapters and sections in a rich-text
   editor (headings, lists, tables, pictures; paste from Word), draft →
   publish revision, automatic section-level change tracking, a revision
   record per revision.
3. **Manual output** — generated PDF with the revision in every page
   footer and a change bar beside changed text; Record of Revisions and
   List of Effective Pages generated; **Print changed pages**; export `.odt`.
4. **Packages** — office exports new/changed documents (files and in-app
   manuals) as one package file; vessel imports it; the master's receipt
   confirmation comes back as a package. A package can itself be a small
   SQLite file, so no zip library is needed. Designed to follow `CLAUDE.md`
   §6 (UUIDs, `origin_node`, `revision`) so the later ship-to-shore sync can
   reuse it.

Each step leaves a working module (`CLAUDE.md` §1).

## 3. Architecture, for the whole module

- Folder `src/modules/documents/` with `domain/ data/ service/ ui/`, and a
  `DocumentsModule` implementing `IModule`, registered with one line in
  `ModuleRegistry` (`CLAUDE.md` §7).
- **Never** includes anything from `modules/certificates/` (`CLAUDE.md` §5).
  Shared needs go through `core/` (e.g. `ManagedFileStore`, `Vessel`).
- Tables named with a `document_` prefix where needed; migrations continue
  the one global numbered folder (next: `012_…`). `CLAUDE.md` §5 gets a
  line saying the documents module owns them.

### 3.1 The vessel selector — a hook, not more direct wiring

Today `MainWindow` finds the certificate screen by type (`qobject_cast` to
`CertificateListWidget`) to rescope it when the toolbar vessel changes.
Doing the same for documents would mean the shell naming a second module's
screen, and `CLAUDE.md` §7 says adding a module must require zero changes to
existing modules.

Add to `IModule` a **non-pure** virtual with an empty default:

```cpp
// Called when the toolbar's vessel selection changes (OFFICE mode), and once
// at start-up. Default: nothing. A module whose screen depends on the
// selected vessel overrides it.
virtual void setSelectedVessel(const QString& vesselId) {}
```

`MainWindow` calls it on every module. `DocumentsModule` overrides it;
`CertificatesModule` is **not** changed — it keeps its existing wiring, and
moving it onto the hook is a later tidy-up, not part of this module.

## 4. Step 1 — the library

### 4.1 Data

`document`:
- `id`, `vessel_id` (**NULL = fleet-wide**; no foreign key, same reasoning
  as `certificate.vessel_id`), `category` (`CHECK` in `'SMS'`,
  `'CIRCULAR'`, `'PLAN_MANUAL'`, `'REPORT'`), `reference` (optional, e.g.
  `SMS-03`, `Circular 12/2026`), `title` (required), `notes` (optional),
  plus the standard audit/sync columns (`CLAUDE.md` §6.5).

`document_version`:
- `id`, `document_id`, `sequence` (1, 2, 3… per document; the highest
  non-deleted one is **current**, all others are **superseded**),
  `revision_label` (optional, e.g. `Rev 5`), `revision_date` (optional
  `YYYY-MM-DD`, information only), `change_note` (optional one line — "what
  changed"), `file_path` (**relative** to the documents folder, `CLAUDE.md`
  §6.9), `original_file_name`, plus audit/sync columns.

"Current" is computed from `sequence`, never stored (`CLAUDE.md` §6.7).

### 4.2 Where the files live

One **documents folder** per installation, chosen in Settings, holding:

```
<documents folder>/Fleet/<CATEGORY>/…
<documents folder>/<IMO>/<CATEGORY>/…
```

Each version is its own file and is **never overwritten** — a new version
is a new file, so every old version stays openable. File names are built
from the reference (or title) and the sequence, e.g.
`SMS-03__v5.pdf`; reuse `ManagedFileStore` and the naming helpers in
`core/` rather than writing new file code.

**The folder's path is stored in `QSettings`**, not the database: it is a
path on *this* computer (same reasoning as the backup folder). After a
restore on another computer the user chooses the documents folder there,
just as each vessel's attachment folder is re-chosen.

**Not in the database backup.** Uploaded files stay in the folder, like
certificate attachments; `docs/machine-setup.md` gets a line about copying
the documents folder when this ships. (Manuals written in the editor, from
step 2, live *in* the database and are in every backup.)

### 4.3 Who can change what

- **OFFICE:** sees and edits everything.
- **VESSEL:** sees fleet-wide documents **plus its own vessel's**; may add
  and change only its own vessel's documents. Fleet-wide documents are
  **read-only** on a vessel — they will arrive from the office in step 4.
- Enforced in the repository (`CLAUDE.md` §3), not only in the screen.

### 4.4 The Documents screen

A sidebar entry **Documents**, after Certificates.

- **Scope:** in OFFICE mode the list shows **fleet-wide documents plus the
  vessel selected in the toolbar**; a VESSEL installation shows fleet-wide
  plus its own. A **Scope** column says "Fleet" or the vessel name.
- **Category tabs** above the table, like the certificate list:
  `All | SMS | Circulars | Plans & manuals | Reports`, each with a count.
  No red marks (no dates in this module). Opens on All.
- **Search box:** filters by title or reference as you type.
- **Columns:** Category, Reference, Title, Scope, Current version (label or
  `v<sequence>`), Revision date, Versions (count).
- **Actions:**
  - **Add Document…** — scope (fleet-wide / selected vessel; vessel only on
    a VESSEL installation), category, reference, title, notes, and the first
    file with its optional revision label, date and change note.
  - **Add New Version…** — choose a file, optional label/date/note; it
    becomes current, the previous one becomes superseded.
  - **Open** (double-click) — opens the current version in the computer's
    normal viewer (`QDesktopServices`).
  - **Versions…** — every version, newest first, current one marked, each
    openable; superseded ones are the archive.
  - **Edit Details…** — reference, title, notes, category.
  - **Delete…** — soft delete (`CLAUDE.md` §6.4). The files stay in the
    documents folder: an SMS record is not something to destroy by
    accident. The confirmation says so.
- If no documents folder is chosen yet, Add Document / Add New Version say
  so and point to Settings.

### 4.5 Settings

A **Documents folder** row on the Settings page (Choose…), built as its own
small widget like `AttachmentFolderSettings`. Changing it later does **not**
move existing files in step 1 — it says so; moving is a later addition if
needed.

### 4.6 Tests

All on temporary folders and databases, never the real ones (`CLAUDE.md`
§10.1):
- Repository scope: OFFICE sees all; VESSEL sees fleet-wide plus its own
  and nothing of another vessel; VESSEL cannot add, edit, version or delete
  a fleet-wide document (refused in the repository).
- Versions: first version is current; adding one makes it current and the
  old one superseded; both files exist and are distinct; soft-deleting a
  document hides it and its versions and leaves the files.
- File naming and placement under `Fleet/` vs `<IMO>/` and category.
- The `IModule::setSelectedVessel()` hook reaches the documents screen;
  the existing certificate selector tests pass unchanged.
- Category tabs and search filter the list; counts correct.

### 4.7 Not in step 1

The editor, generated PDFs, page printing, packages, acknowledgement, any
dates/alerts, moving files when the folder changes.
