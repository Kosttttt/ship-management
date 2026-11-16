# Documents step 2 — history, compare, restore

Version 1.0. Step 2 of `docs/document-management-spec.md` §3. Step 1
(spaces, page tree, editor, draft → publish) passed all seven of the
developer's on-screen checks.

**No schema change.** Every published revision is already stored and never
changed (`document_page_revision`, `document_space_revision`,
`document_image`). This step only reads them.

---

## 1. What it gives the developer

- **Page history:** every published revision of a page — who, when, the
  note, and in a Manual space which manual revision it belongs to. Any
  revision can be opened read-only.
- **Record of Revisions** for a Manual space: Rev 1, Rev 2, … with date,
  note and the pages each one added, changed or withdrew.
- **Compare** any two revisions of a page, or the draft against what is
  published, with the differences highlighted.
- **Restore** an old revision of a page into its draft — never published
  directly.

## 2. Page history

A **History** button on the page (right panel), next to Edit. It shows the
page's revisions, newest first:

```
Rev  Manual rev   Date          By       Note
 3   Rev 8        12 Nov 2026   SYSTEM   Fire drill frequency updated
 2   Rev 5        03 Mar 2026   SYSTEM   Muster list reference
 1   Rev 1        10 Jan 2025   SYSTEM   First issue
```

"Manual rev" only in a Manual space. "By" is `created_by` — `SYSTEM` until
users exist (`CLAUDE.md` §11); shown anyway so the column is there when
they do.

From the list: **View** (that revision, read-only, with its own pictures),
**Compare with previous**, **Compare…** (pick any two), **Restore…** (§5).

## 3. Record of Revisions (Manual spaces)

On a Manual space, **Revisions…** (next to Publish revision…) lists the
space revisions, newest first: number, date, note, and under each one the
pages it covered, marked **new**, **changed** or **withdrawn**. A page
there opens that page's revision; a changed page offers **Compare with
previous**.

This is the screen-form of the printed Record of Revisions that step 5
will produce — same data, same order.

## 4. Compare

### 4.1 What is compared

Two versions of the same page: any two published revisions, or **the
draft against the latest published revision** — a **Compare with
published** button while a page has unpublished changes, so the editor can
see "what have I changed?" before publishing. The Manual **Publish
revision…** dialog gets the same button next to each listed page.

### 4.2 How it is shown — inline, on the newer version

The newer version is shown as a page, with the changes marked in it:
- **added text** — green background;
- **removed text** — red, struck through, left in place where it was;
- **a paragraph that only changed formatting** (same words, e.g. made
  bold) — a thin coloured bar in the margin and the note "formatting
  changed";
- **a picture added / removed / replaced** — a labelled box around it.

A one-line legend at the top: "Comparing Rev 2 (03 Mar 2026) → Rev 3
(12 Nov 2026)", and **Close**. Inline rather than side-by-side: it reads
like the page itself, works at any window width, and is what step 5 will
print as change bars.

### 4.3 The comparison rules — in the domain layer, with no widgets

A pure function (no Qt widgets, no database — `CLAUDE.md` §4 rule 1)
takes two contents and returns a list of marked pieces:
1. Split each content into **blocks** in reading order: paragraphs,
   headings, list items, table cells, pictures.
2. Match blocks between old and new with a longest-common-sequence diff on
   block text (and picture id) — so an inserted paragraph shows as one
   insertion, not as every following paragraph "changed".
3. For a pair of blocks matched as changed, diff them again **word by
   word**, so a one-word edit marks one word.
4. Same text, different formatting → "formatting changed".

Manuals are small (a page is a few hundred blocks at most), so a simple
dynamic-programming diff is fast enough; no outside library.

A moved paragraph shows as removed in one place and added in another —
fine, and honest. Table structure changes (a row added) show as the new
cells added.

## 5. Restore

**Restore…** on a revision copies its **title and content into the page's
draft**. Nothing is published: the page now has unpublished changes, the
editor can look and adjust, and it goes out the normal way — on its own in
a Collection space, as part of the next manual revision in a Manual space.
That keeps ISM's rule that only a published revision counts.

- If the page already has a draft, ask first: "This replaces your
  unpublished changes to <page>."
- Pictures need nothing copied — the old revision refers to images that
  still exist (`document_image` is never changed).
- Only where the space is editable (`mayEdit()` — a vessel can't restore
  into a company space). History, View and Compare work everywhere,
  read-only included.

## 6. Not in this step

Restoring a page that was **withdrawn** from a manual (its history stays
readable through the Record of Revisions; bringing it back can come later),
comparing two different pages, comparing a whole manual at once, printing
(step 5).

## 7. Tests

All on temporary databases; the comparison rules with no database at all.
- **Comparison (domain):** identical → no marks; one word changed → only
  that word; a paragraph inserted in the middle → one insertion, the
  paragraphs after it unmarked; a paragraph removed; formatting-only
  change; a picture added, removed, replaced; a table cell edited; a list
  item added. Prove the "inserted in the middle" test fails against a naive
  position-by-position comparison, so the sequence matching is really
  doing its job.
- **History:** revisions newest first, with the manual revision number in a
  Manual space and none in a Collection space.
- **Record of Revisions:** each manual revision lists exactly the pages it
  added, changed and withdrew (reuse step 1's Rev 1 / Rev 2 / withdrawal
  scenario).
- **Restore:** content and title land in the draft; nothing is published;
  an existing draft is replaced only after confirmation; refused on a
  read-only space (in the repository/service, not just the button);
  the restored draft then publishes as a new revision, never by
  rewriting an old one.
- **Compare with published:** marks exactly the draft's changes.

Follow `CLAUDE.md` §9 on file sizes as before.
