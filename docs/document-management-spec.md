# Document Management — module specification

Version 2.0. **Replaces version 1.0** (a table of uploaded files with
versions, built as step 1 in `47179f5`). After seeing it, the developer
decided the module should not look and work like Certificate Control, but
like a simple **Confluence**: spaces, a page tree, pages read and edited in
place, history. Version 1.0's library is removed completely (§4.1).

---

## 1. Decided with the developer

- **Confluence-style:** spaces → page tree → pages, read and edited inside
  the program.
- **Spaces:** company spaces the office creates (e.g. SMS Manual, Cargo
  Manual, Circulars, Forms), plus **one space per vessel, created
  automatically**.
- **Revisions of a manual are numbered for the whole manual:** "SMS Manual
  Rev 8" changes, say, 5.2 and 7.1; those pages are edited as drafts and
  Rev 8 is published for the manual in one go, with one note. Each page
  shows the revision it was last changed in. This matches ISM document
  control — only a published revision counts — and gives the Record of
  Revisions and List of Effective Pages later.
- **First version's features:** page tree + rich editor; history +
  compare; attachments + search; links between pages + templates.
- **Later:** printed output (PDF with change bars, Record of Revisions,
  List of Effective Pages, Print changed pages, `.odt` for Word); office →
  vessel packages with the master's receipt confirmation.
- **No expiry/review dates** in this module for now.

Why manuals are written *in* the program rather than uploaded as PDFs:
Qt's PDF module can't be built with MinGW, this project's Windows compiler,
so the program can't print chosen pages of an uploaded PDF — but it can
print any page it lays out itself.

## 2. Two kinds of space

| | **Manual space** | **Collection space** |
|---|---|---|
| For | SMS Manual, Cargo Manual, any controlled manual | Circulars, Forms, each vessel's space |
| Publishing | the **whole space** gets Rev N; every page changed since Rev N−1 is included | **each page** is published on its own |
| Page shows | "Rev 8 · 12 Nov 2026" — the manual revision it last changed in | "Rev 3 · 12 Nov 2026" — its own count |
| Created by | the office | company: the office; vessel spaces: automatic |

Vessel spaces are always Collection spaces.

## 3. Build order for this module

1. **Spaces, page tree, rich editor, draft → publish** — *this spec, §4–§6*.
2. **History + compare + restore** — every published revision of a page and
   of a manual; compare any two with changes highlighted; restore an old
   revision into the draft.
3. **Attachments + search** — files (Word, PDF, anything) attached to a
   page with every version kept; search across page titles and text.
4. **Links + templates** — links from one page to another; page templates
   (circular, procedure, …).
5. **Printed output** — PDF and `.odt` of a manual; revision in page
   footers; change bars; Record of Revisions; List of Effective Pages;
   **Print changed pages**.
6. **Office → vessel packages** — export/import of published revisions;
   the master's receipt confirmation sent back the same way.

Steps 2–6 get their own short specs when they come up. Each step leaves a
working module.

## 4. Step 1 — cleaning up, and the data

### 4.1 Remove version 1.0 completely

- **New migration that drops** `document_version` and `document` (migration
  012 has already run on the developer's machine and is never edited —
  `CLAUDE.md` §6.6). Any test documents entered in them are gone; their
  files stay in the old documents folder on disk, where the developer can
  delete them by hand.
- Remove the version-1.0 code and tests: the list screen, its dialogs,
  actions, category tabs, file store, repository and its helpers, and the
  **Documents folder** row in Settings.
- **Keep** `IModule::setSelectedVessel()` — it is generic and harmless.
  The new Documents screen doesn't need it (spaces are picked in its own
  panel) and needn't override it.
- **Keep** the module's place: `src/modules/documents/`, `DocumentsModule`,
  the `ShipDocuments` library that links only `ShipCore`, one line in
  `ModuleRegistry`.

### 4.2 Tables (all `document_` prefixed, standard audit/sync columns, `CLAUDE.md` §6)

- **`document_space`** — `name`, `kind` (`MANUAL` / `COLLECTION`),
  `vessel_id` (set only for a vessel's automatic space), `description`,
  `position` (order in the list).
- **`document_page`** — `space_id`, `parent_id` (NULL = top of the tree),
  `position` (order among siblings), and **the draft**: `draft_title`,
  `draft_content` (empty when there is no unpublished change). The
  published title and content are those of the page's latest revision —
  not copied onto the page row. A page with no revision yet is a draft
  page.
  `withdraw_pending` (a published page marked for removal in a manual's next
  revision — §6.3).
- **`document_page_revision`** — immutable: `page_id`, `number` (the page's
  own count), `space_revision_id` (set in a Manual space), `title`,
  `content`, `note`.
- **`document_space_revision`** — immutable, Manual spaces only: `space_id`,
  `number` (Rev N), `note`.
- **`document_image`** — immutable: `data` (BLOB), `mime_type`. Pictures
  live **in the database**, so pages and their pictures are in every backup.
  Content refers to an image by id; a changed picture is a new row, so old
  revisions still show their own.

"Has unpublished changes", the current revision of a page and of a manual
are **computed**, never stored (`CLAUDE.md` §6.7).

Content is stored as the HTML `QTextDocument` produces.

### 4.3 Who may change what

- **OFFICE:** everything. Creates, renames and deletes company spaces.
- **VESSEL:** sees company spaces **read-only** (they will arrive from the
  office by package in step 6) and **its own vessel space**, which it may
  edit. Other vessels' spaces are not shown.
- **Vessel spaces** are created automatically for each active vessel the
  first time the Documents screen needs them; an archived vessel's space is
  hidden, not deleted.
- Enforced in the repository (`CLAUDE.md` §3), not only the screen.

## 5. Step 1 — the screen

```
┌──────────────┬─────────────────────────────────────────────┐
│ SPACES       │ SMS Manual › Chapter 5 › 5.2 Fire Drills     │
│  SMS Manual  │ Rev 7 · 12 Nov 2026     [Edit] [Publish…]    │
│  Circulars   │──────────────────────────────────────────────│
│  V UNO       │ page content                                 │
│  V TRE       │                                              │
│──────────────│                                              │
│ PAGES        │                                              │
│ ▾ Chapter 5  │                                              │
│    5.1       │                                              │
│    5.2  ◀    │                                              │
└──────────────┴─────────────────────────────────────────────┘
```

- **Left, top:** spaces — company spaces first, then vessel spaces. Office:
  **New Space…** (name, Manual or Collection), Rename, Delete (only an
  empty space, or with a confirmation that soft-deletes its pages).
- **Left, bottom:** the page tree of the selected space. **New Page**
  (child of the selected page, or top-level), **Move up / Move down**,
  **Move to…** (pick a new parent), **Delete…** (§6.3). A page with
  unpublished changes shows a small marker in the tree.
- **Right:** a breadcrumb, the page's revision line, then the page.
  - **Reading** shows the **published** content. If a draft exists, a bar
    says "This page has unpublished changes" with **Continue editing** /
    **Discard draft**. A never-published page shows its draft, marked
    "Draft — not published".
  - **Edit** turns the page into the editor (§6.1); **Save** keeps the
    draft, **Cancel** leaves it as it was.

## 6. Step 1 — editing and publishing

### 6.1 The editor

`QTextEdit` with a toolbar: paragraph style (Normal, Heading 1–3), bold,
italic, underline, bulleted and numbered lists, insert table (rows ×
columns) with add/remove row and column, insert picture, undo/redo.

- **Paste from Word** keeps the structure — paragraphs, bold/italic/
  underline, lists, tables, pictures — and **drops Word's fonts, sizes and
  colours**, so every page looks the same. (Word headings may arrive as big
  bold text rather than Heading 1–3; reapplying the heading style is fine.)
- **Pictures** (inserted or pasted) go into `document_image`. Very large
  pictures are scaled down on the way in so the database doesn't balloon;
  the limit is Claude Code's call, stated in the report.

### 6.2 Publishing

- **Collection space — per page:** **Publish…** asks for an optional note;
  the page gets its next revision number.
- **Manual space — the whole manual:** on the space, **Publish revision…**
  lists every page with unpublished changes (new, changed, or marked for
  withdrawal), asks for a **note** (required) and shows the new number
  ("Rev 8"). Publishing creates the space revision and one page revision
  for each listed page, all in **one transaction** — a half-published
  manual must be impossible. Pages without changes keep their old revision.
  Individual pages in a Manual space have no Publish of their own.
- After publishing, the draft is cleared (the published content is now
  what Edit starts from).

### 6.3 Deleting pages

- A **never-published** page: deleted straight away (soft delete).
- A **published page in a Collection space**: soft-deleted after a
  confirmation.
- A **published page in a Manual space**: marked **"to be withdrawn"**
  (struck through in the tree); it disappears when the next manual
  revision is published, and that revision lists it as withdrawn —
  removing a section is a change to the manual like any other. "Undo
  withdraw" is available until then.
- A page with children: the confirmation names how many pages go with it.

## 7. Step 1 — tests

All on temporary databases (`CLAUDE.md` §10.1).

- Migrations: the drop runs cleanly on a database that has 012's tables
  with rows in them, and on one that doesn't.
- Access: OFFICE edits all; VESSEL edits only its own space, sees company
  spaces read-only, never sees another vessel's space; refused in the
  repository.
- Vessel spaces: created once per active vessel (running it twice creates
  no duplicates); archived vessel → space hidden.
- Tree: create child and top-level pages; move up/down; move to a new
  parent; a page can't be moved under itself or its own child.
- Draft/publish, Collection: edit → draft; publish → revision 1, draft
  cleared; edit again → revision 2.
- Draft/publish, Manual: edit two of three pages → Publish revision lists
  exactly those two → Rev 1 with two page revisions; the third page keeps
  no revision; a failed publish leaves nothing half-written.
- Withdraw: a published manual page marked for withdrawal is still there
  until publish; after publish it's gone and Rev N lists it as withdrawn;
  Undo withdraw before publish restores it.
- Paste from Word: pasted HTML with fonts/colours keeps bold/lists/tables
  and loses the fonts and colours.
- Pictures: stored in `document_image`; an edited picture is a new row;
  the older revision still refers to the old one.

## 8. Not in step 1

History/compare/restore, attachments, search, links, templates, printed
output, packages, acknowledgement.
