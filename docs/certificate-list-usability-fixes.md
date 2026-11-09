# Certificate list usability fixes

Three small, related fixes to the certificate list screen, raised by the
developer during his first real use of the Release build on real fleet
data (not covered by any earlier step's spec). Bundled into one round
because they're all small and all touch the same two files
(`CertificateListWidget`, `CertificateListActions`), not because they're
one feature — treat each section below independently.

## 1. Column widths: make them resizable, and remember them

**Problem.** Every column in the certificate table is currently either
`QHeaderView::Stretch` or `QHeaderView::ResizeToContents`
(`CertificateListWidget.cpp`, constructor). Both of those modes disable
manual dragging — this is not a missing feature so much as an active
setting — and `Stretch` divides the full width equally regardless of what
a column holds, so a long certificate name gets clipped with no way to
see the rest of it.

**Fix.**

- Switch every column to `QHeaderView::Interactive`, so the user can drag
  column borders the way they would in a spreadsheet. `setStretchLastSection(true)`
  is worth considering so the final column (`Document`) absorbs any
  leftover width instead of leaving dead space, but isn't required.
- Give `Name` more starting width than the others get by default —
  it's the one column that routinely needs it. Exact pixel width is your
  call; something that comfortably fits a long certificate name at this
  window's default size is the goal, not a specific number.
- **Remember the user's own sizing across restarts.** `QHeaderView` has
  exactly this built in: `saveState()`/`restoreState()` capture section
  sizes (and order, and sort column) as a `QByteArray`. Store that via
  `QSettings` — a plain default-constructed `QSettings` already lands in
  the right place, since `main.cpp` already calls
  `QCoreApplication::setOrganizationName("Ship Management")` /
  `setApplicationName("Ship Management System")`. This is UI-only
  preference, not business data, so it belongs in `QSettings`, not in
  `app_setting`/the SQLite database — nothing about it needs to survive
  a ship-to-shore sync or mean anything on a different installation.
- Sequence at construction: set the column resize modes and the default
  widths and the default sort (as today), *then* attempt to restore a
  saved header state if one exists — a returning user's own last sizing
  should win over the hardcoded defaults.
- Save on every resize, not only on a clean shutdown (`sectionResized`
  is the obvious signal to hang it from) — cheap, and it means a crash or
  a force-quit doesn't lose today's resizing. Saving again in the
  widget's destructor as a fallback is fine too, but the live save is
  the one that actually matters.

## 2. Multi-select + bulk delete

**Problem.** `CertificateListWidget` sets
`m_table->setSelectionMode(QAbstractItemView::SingleSelection)`, so only
one certificate can ever be selected, and `CertificateListActions::deleteCertificate()`
only ever acts on one. After importing roughly 250 certificates twice by
accident (see below), there is no reasonable way to clean that up from
inside the application today.

**Fix.**

- Switch the table to `QAbstractItemView::ExtendedSelection` (ordinary
  Ctrl/Shift-click multi-select).
- `CertificateListActions`'s single-selection helpers
  (`selectedRow()`/`selectedDataItem()`/`selectedCertificateId()`/
  `selectedExtensionExpiry()`) should keep meaning exactly what they mean
  today — "the one selected certificate" — by now returning the
  not-selected state whenever zero *or more than one* row is selected,
  not just zero. That one change is what correctly disables Renew, Start
  Short-Term Extension, and the Attachment button the moment more than
  one row is selected, with no separate logic needed: all three are
  inherently single-certificate actions and already go through these
  helpers.
- Delete is the one action that should work on any non-empty selection.
  Add a plural accessor (every selected row's certificate id) for it to
  use, and enable the Delete button whenever that's non-empty rather than
  via the single-selection helper.
- `deleteCertificate()`: keep today's behaviour byte-for-byte when exactly
  one certificate is selected (same confirmation wording, same survey
  count, same attachment-deletion warning). For more than one, confirm
  once — naming the count, and the names too if the list is short enough
  to show without the dialog becoming unwieldy, your call on the cutoff —
  then loop the existing `CertificateDeletionService::deleteCertificate()`
  call per certificate, exactly as today's single path already does.
  Collect which ones failed (both outright failures and the "deleted, but
  a file remains" case) and report one combined summary at the end rather
  than a dialog per row.
- **Do not wrap the loop in an outer transaction.** Each call already
  opens and commits its own transaction inside
  `CertificateRepository::softDeleteWithChildren()`, and SQLite has no
  nested transactions — `TransactionScope`'s own header comment says as
  much. A delete that's "best effort per certificate" is also the right
  behaviour here: if row 7 of 20 fails, the other 19 should still go
  through rather than being rolled back over one failure.
- `emit listChanged()` once at the end if at least one certificate was
  actually removed.

## 3. CSV import: optional placeholder issue date

**Context.** The developer hand-built a CSV from an existing spreadsheet
for his first bulk import. Several rows had no issue date at all — mostly
non-certificate items that had been mixed into the same sheet (a planned-
maintenance reminder, a spare part) that he nonetheless wants tracked in
this system under their expiry date. Today those rows come back Invalid
("An issue date is required"), which is correct and should stay the
default — but he'd like an easy way to say "yes, I know, use a placeholder"
for an entire file in one step instead of hand-editing the CSV, which is
what I did for him manually this time.

**Fix.**

- Add a checkbox to `CertificateImportPreviewDialog`, something like
  *"Fill in a placeholder issue date (1990-01-01) for rows with none, and
  flag it in Notes"* — **unticked by default**. A missing issue date
  should still block an ordinary import unless the user explicitly asks
  for this, since it's sometimes a real data-entry mistake worth catching
  rather than something to paper over silently.
- When ticked, every row whose *only* problem is a missing issue date
  should be reclassified live, in the same preview table that's already
  open — not require re-picking the file or reopening the dialog. A row
  that has a missing issue date *and* some other problem should stay
  Invalid; the checkbox only rescues the one specific case.
- The placeholder itself: issue date `1990-01-01`, and prepend to the
  row's own Notes something equivalent to what I used by hand: *"No real
  issue date was recorded for this item — 1990-01-01 is a placeholder,
  not a fact."* Exact wording is yours to refine, but the note has to
  survive into whatever gets written to the certificate, since it's the
  only thing that will ever tell anyone later that the date is fake.
- Where this logic lives matters for the usual reason: "a missing issue
  date can be patched with a flagged placeholder" is a business rule, not
  a dialog's business, so it belongs in the domain or service layer
  (`CertificateValidation`/`CertificateCsvFormat`/`CertificateCsvImporter`
  — wherever fits best once you're in the actual code), not written
  directly inside `CertificateImportPreviewDialog`. The dialog's job is
  to own the checkbox and ask that layer to re-evaluate the affected rows
  when it's toggled; it shouldn't itself decide what the placeholder date
  is or what the note says.
- `validateCertificate()` is a plain domain function with no Qt-widget or
  SQL dependency, so calling it from the dialog to re-check a patched row
  is fine under `CLAUDE.md` §4 — the rule that matters is no SQL and no
  widgets *inside* the domain layer, not "the UI may never call a domain
  function."

## Not in scope for this round

- A general dedup-by-name-and-number fallback for CSV import, in case a
  file's `Id` column never matches (raised, not pursued): risks a false
  match far more than it helps, and the actual fix — export before you
  edit, rather than re-editing a hand-built file forever — already
  exists and just needed explaining. Worth remembering if it comes up
  again, not worth building speculatively now.
- Anything about *why* the developer's first import duplicated every
  certificate: that's expected behaviour from `create()` always minting a
  fresh id, already covered in the Step 13 spec, and not a bug to fix
  here.
