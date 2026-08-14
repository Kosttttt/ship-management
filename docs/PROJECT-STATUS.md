# Project Status

**Update this file at the end of every completed step.** It is the handover
document for any new session — chat, Cowork or Claude Code. Sessions do not
remember each other; this file is the memory.

Read alongside `CLAUDE.md` (architecture rules) and
`docs/certificate-control-spec.md` (module behaviour).

---

## Who is working on this

The developer is an experienced maritime professional and a **complete beginner
in C++**. Explain C++ carefully and in plain language; defer to him on domain
rules (certificates, surveys, HSSC, fleet operations). He knows that subject far
better than you do.

Working style agreed:
- One step at a time. Finish and verify before starting the next.
- Explain every file after writing it, and say *why* each choice was made.
- State trade-offs and recommend one option; never silently pick.
- Commit after each working step.

## Roles of each tool

| Tool | Job |
|---|---|
| **Claude Code** (Code tab) | Writes the C++. Runs builds and tests. |
| **Cowork / chat** | Architect and mentor. Reviews, discusses, revises specs and this file. Does **not** write production code. |
| **Qt Creator** | Where the developer reads, runs (Ctrl+R) and debugs (F5) the code. |

---

## Environment

| Item | Value |
|---|---|
| Repository | `D:\dev\ship-management` (GitHub: Kosttttt/ship-management, private) |
| OS | Windows |
| Qt | 6.11.1, MinGW 64-bit kit |
| Compiler | MinGW GCC 13.1.0 |
| CMake / Ninja | 3.30.5 / bundled with Qt |
| App data folder | `%APPDATA%\Ship Management\Ship Management System\` |
| Database file | `ship-management.db` in that folder |
| Log file | `logs\ship-management.log` in that folder |

Building from a terminal requires Qt's tools on PATH first — see step 2 notes in
git history. Qt Creator does this automatically and is the normal way to build.

Known open item: F5 debugging in Qt Creator does not stop on breakpoints. Qt
Creator also shows a warning that GDB is inappropriate for the binary format —
that warning is a false alarm (the executable is MinGW-built; the earlier
`libstdc++-6.dll` error proves it). Ctrl+R works. Revisit when a real need
arises.

The developer's shell is Windows PowerShell, where `&&` does not chain
commands the way it does in bash/PowerShell 7+ (`The token '&&' is not a
valid statement separator in this version`). Any suggested commit command
needs two separate commands, or `;`, instead of
`git add -A && git commit -m "..."`:
```
git add -A
git commit -m "..."
```

---

## Completed steps

### Step 1 — Qt shell ✅
Minimal Qt 6 Widgets application with CMake. `MainWindow`, `main.cpp`, folder
structure from `CLAUDE.md` §8. Builds clean, window opens.

### Step 2 — Core infrastructure ✅
- `core/Logger` — replaces Qt's message handler, writes console + rotating file
  (5 MB × 5 backups), UTC timestamps, mutex-protected for future threading.
- `core/Database` — owns the single SQLite connection. Path resolved via
  `QStandardPaths::AppDataLocation` (no platform-specific code).
  `PRAGMA foreign_keys = ON`.
- `core/MigrationRunner` — applies numbered `.sql` files in order, records them
  in `schema_version`, one transaction per migration, SHA-256 checksum guard
  against edited migrations, statement splitter that respects comments and
  quoted strings.
- `migrations/001_create_installation.sql` — the `installation` table with a
  `singleton` constraint and a CHECK enforcing the OFFICE/VESSEL ↔ `vessel_id`
  rule.
- Migrations are compiled into the binary via Qt resources (`:/migrations/…`).
  **Adding a new migration means adding a line to `CMakeLists.txt` — forgetting
  this means the migration silently never runs.**
- `core` builds as a static library `ShipCore`, linked by both app and tests.
- 13 unit tests (Qt Test), all passing.

Two defects found during verification and fixed:
1. `id TEXT PRIMARY KEY` permitted NULL — in SQLite only `INTEGER PRIMARY KEY`
   is implicitly NOT NULL. Now explicit. This affects **every future table**;
   always write `TEXT PRIMARY KEY NOT NULL`.
2. Startup errors were logged after the modal dialog, so force-closing the
   dialog left no trace. Now logged before.

### Step 3 — First-run wizard & vessel table ✅
Full design in `docs/first-run-wizard-spec.md`. Built and verified end to end,
including driving the real wizard through the GUI, not just unit tests.

- `migrations/002_create_vessel.sql` — the `vessel` table, brought forward
  from step 4 because the wizard needs a vessel row to point at.
- `core/ImoNumberValidator` — the check-digit rule, a pure function with no
  widgets and no SQL.
- `core/InstallationContext` / `core/InstallationRepository` — the read-only
  context and the one file in this step containing SQL. Both `create*`
  methods write in a single transaction.
- `app/FirstRunWizard`, `InstallationModePage`, `VesselIdentityPage` — the
  `QWizard`, one class per file.
- `core/SqlStatementSplitter` extracted out of `MigrationRunner.cpp`,
  resolving the technical-debt item recorded after step 2. `MigrationRunner.cpp`
  is now 272 lines, `SqlStatementSplitter.cpp` 51 — both comfortably under the
  ~300-line limit in `CLAUDE.md` §9.
- 53 unit tests (9 splitter, 13 migrations, 16 IMO validator, 15 installation
  repository), all passing, run against an in-memory database with the real
  migrations applied.

One defect found during GUI verification and fixed:

Pressing Enter reaches `QDialog::accept()` directly, bypassing whichever page
the Finish/Next button was showing. The first fix only checked whether the
current page was complete, which wasn't enough — the very first Enter on
page 1 called `accept()` instead of advancing to page 2, because the mode
page is always complete. `FirstRunWizard::accept()` now checks two things:
the current page must be complete, **and** it must actually be the last page
(`nextId() == -1`); otherwise it calls `next()` instead. Confirmed no errors
logged and the full keyboard path completes correctly afterward.

### Step 4 — Vessel CRUD ✅
Full design in `docs/vessel-crud-spec.md`. Built and verified end to end,
including screenshots of both screens and a real Save through the GUI (click
guarded by a foreground-window check after an earlier attempt using global
keystrokes leaked a keystroke into an unrelated window — no harm done, but
global `SendKeys`-style input is now avoided for this reason).

- `core/Vessel` / `core/VesselRepository` — the first consumer of
  `InstallationContext::vesselScope()`. The scope filter and the
  `is_deleted = 0` exclusion are both applied inside `list()`/`findById()`
  themselves, not left to the screen. `create()` refuses under a VESSEL
  context; `update()` refuses any id other than the installation's own
  vessel under VESSEL mode.
- `update()` is the project's first `UPDATE` statement: it sets
  `revision = revision + 1` in the SQL itself and never mentions
  `created_at`/`created_by`, so those columns cannot be touched by accident.
- Blank optional fields (`call_sign`, `gross_tonnage`, `port_of_registry`,
  `flag_state`) store as SQL `NULL`, not `''`/`0`, so "not entered" stays
  distinguishable — same convention `InstallationRepository` already used
  for `vessel_id`.
- `app/VesselEditForm` — one `QWidget` reused by both `VesselEditDialog`
  (OFFICE, Add and Edit) and `VesselDetailPage` (VESSEL, no dialog chrome,
  no list to return to). Gross tonnage uses a `QSpinBox` with minimum 0, so
  "never negative, never a fraction" is enforced by the widget itself.
- `app/ImoNumberMessages` extracted from the step-3 wizard so the wizard and
  this form report the same IMO problem with the same wording.
- `src/app/` became a static library, `ShipApp`, so the widget test can link
  the same code the application runs. `main.cpp` is the only file left
  outside it.
- 85 unit tests total, all passing, across 7 suites. `tst_VesselRepository`
  was split into `tst_VesselRepositoryScope` / `tst_VesselRepositoryWrites`
  with a shared `VesselTestSupport.h` fixture, once the combined file passed
  the ~300-line guideline.

One defect found during verification and fixed:

`create()`/`update()` checked IMO uniqueness by excluding the row's own id
with `id != ?`, binding a default-constructed (null) `QString` for a new
vessel. A null `QString` binds as SQL `NULL`, and `id != NULL` evaluates to
unknown rather than true in SQL — so the exclusion clause silently matched
nothing, and every duplicate IMO number fell through to the database's raw
`UNIQUE` constraint error instead of the friendly message spec §6 requires.
Fixed by adding the `AND id != ?` clause only when there is an id to
exclude. A test now asserts the string `"UNIQUE"` never reaches the user.

### Step 5 — Certificate CRUD ✅
Full design in `docs/certificate-crud-spec.md`. Built and independently
verified file-by-file against that spec (migration, domain, repository,
both UI files, `IModule`/`ModuleRegistry`, `MainWindow`) — not just from
Claude Code's own report.

- `migrations/003_create_certificate.sql` — the `certificate` table. Every
  certificate carries its own `name`, `category`, and survey-rule fields
  directly, per the architecture decision below. `expiry_date` is nullable;
  `previous_certificate_id` is deliberately absent (it belongs to step 9,
  the renewal workflow, since nothing can populate it before then). A CHECK
  constraint backstops the "no expiry means no survey requirement" rule.
- `modules/certificates/domain/Certificate` — a plain struct plus a
  `CertificateCodes` namespace for enum ↔ database-string mapping.
  `CertificateCategory` has a fourth value, `Unset`, alongside the three
  real categories — never stored, used only so the form and repository can
  represent "no category chosen yet". See the decision below.
- `modules/certificates/data/CertificateRepository` — the module's only SQL
  file. Mirrors `VesselRepository`: the VESSEL-mode scope filter is applied
  inside `list()`/`findById()` themselves; `create()`/`update()` refuse
  outright, with a message, for another vessel's certificate under VESSEL
  mode; the no-expiry-with-survey rule is checked with a friendly message
  before the raw CHECK constraint; `update()` bumps `revision` and touches
  only `updated_at`/`updated_by`, exactly as `VesselRepository::update()`
  does.
- `app/IModule`, `app/ModuleRegistry` — the module interface from
  `CLAUDE.md` §7, and the fixed compile-time list of modules. Registering a
  module is one line in `ModuleRegistry`'s constructor. `AlertProvider` is
  only forward-declared — its interface is deferred to step 8, the first
  step with something to report through it.
- `modules/certificates/CertificatesModule` — the first `IModule`. Its
  `migrations()` returns an empty list on purpose: `migrations/` stays one
  flat, globally-numbered folder driven by `CMakeLists.txt`, not scattered
  per module.
- `modules/certificates/ui/CertificateEditForm` — the thirteen editable
  fields, grouped into `QGroupBox` sections inside a `QScrollArea`. The "no
  expiry" checkbox doesn't just grey out the survey checkboxes when ticked,
  it clears them — an invalid combination must not survive hidden behind a
  disabled control, ready to be saved if the box is unticked again later.
- `modules/certificates/ui/CertificateEditDialog` — wraps the form in
  Add/Edit, the same shape as step 4's `VesselEditDialog`. New method
  `setVesselId()` added to the form (see the defect below) alongside
  `setCertificate()`.
- `modules/certificates/ui/CertificateListWidget` — one table per vessel,
  shared by both installation modes (unlike vessels, a single vessel still
  carries dozens of certificates, so there was no reason to split this into
  two screen classes). Three states: an explicit "select a vessel above"
  prompt, a populated table, and an empty-but-scoped table — the prompt is
  a distinct label rather than an empty table, since an empty table would
  read as "this vessel has no certificates," a different fact.
- `app/MainWindow` gained the sidebar (`Vessels` fixed first, then one row
  per registered module) and, in OFFICE mode only, a toolbar vessel
  selector that starts unselected. `MainWindow` wires the selector directly
  to the certificates screen via a `qobject_cast` rather than a general
  "vessel-scoped module" abstraction — documented in the header as the one
  piece of direct wiring the spec sanctions, to be generalised the moment a
  second per-vessel module exists.
- 142 unit tests total, across 13 suites. `tst_CertificateRepositoryWrites`
  was split into `tst_CertificateRepositoryValidation` (what the repository
  refuses) and `tst_CertificateRepositoryWrites` (what it stores) once the
  combined file passed ~300 lines, the same pattern used for
  `tst_VesselRepository` in step 4.

Two defects found during verification and fixed:

1. `CertificateEditDialog`'s Add mode called `setCertificate()` with a
   default-constructed `Certificate`. A default-constructed certificate has
   a null `expiryDate`, which domain-correctly means "never expires" — but
   is the wrong default for a *new* certificate, where most certificates do
   expire. The dialog opened with "does not expire" already ticked. Fixed
   by adding `CertificateEditForm::setVesselId()`, which tells the form
   which vessel it belongs to without touching any field, so Add mode keeps
   the form's own constructor-set defaults (an expiry date five years out).
   Confirmed by reading the fixed code directly: Add mode now calls only
   `setVesselId()`, never `setCertificate()`.
2. The test helper matched checkboxes by fuzzy substring text (`"inter"`),
   which matched `"&Interim certificate"` before
   `"Requires an inter&mediate survey"` and ticked the wrong one. Fixed by
   looking widgets up by Qt object name instead of label text; a new test
   asserts the interim and intermediate-survey checkboxes are genuinely
   distinct controls.

A smaller, harmless deviation from spec noticed on review: `intermediate_mode`
is `NOT NULL DEFAULT 'ADDITIONAL'` rather than the nullable column the spec
suggested. A reasonable simplification — every certificate needs *some*
value there the moment a UI combo box is involved, since a combo always has
a selection — and it matches how the form and repository already treat it.
No action needed.

### Step 5 addendum — vessel selector fix, "No." field, Issue Date column ✅
Full design in `docs/certificate-crud-spec.md` §8, written after the developer
tried step 5 hands-on and found a real defect plus two things he wanted the
list to show. Built and independently verified file-by-file, including
reading the new migration, the domain rule, the repository backstop, the
form validator, the new sort-aware table item, the selector-refresh fix, and
the new/expanded tests — not just from Claude Code's own report.

- §8.1 fix: the OFFICE toolbar vessel selector didn't see a vessel added
  from the "Vessels" screen until restart, because it was populated once at
  `MainWindow` construction and nothing told it to re-read afterwards.
  `VesselListWidget` now emits `vesselsChanged()` after a successful add
  *and* after a successful edit (a rename changes both the label and where
  it sorts). `MainWindow::refreshVesselSelector()` re-reads the fleet and
  restores the current selection **by id**, not position, since inserting a
  vessel can shift where others sit alphabetically. The selector's initial
  population now goes through this same method, so there's one code path
  instead of two that could drift apart.
- §8.2: a new optional `list_number` field ("No.") — a short company
  reference like "15D", used in place of a certificate's full name.
  `migrations/004_add_certificate_list_number.sql` adds the column (a new
  migration, since `003_create_certificate.sql` is already committed and is
  never edited). The format — digits, then letters, nothing else — is
  enforced in one place, `CertificateListNumber` in the domain
  (`pattern()`/`isValid()`/`lessThan()`), and every layer uses that same
  rule: the form's `QLineEdit` validator is built from `pattern()` so an
  invalid keystroke never appears on screen; the repository's `validate()`
  calls `isValid()` as a backstop for anything that arrives another way (a
  future CSV import); the migration's `CHECK` constraint (written with
  `GLOB`, since SQLite has no regular expressions) is the final backstop.
  Sorting is why the format matters at all: as free text, "15D" would sort
  ahead of "3A", because `'1'` precedes `'3'` as a character. A new
  `QTableWidgetItem` subclass, `ListNumberItem`, forwards its `operator<`
  to `CertificateListNumber::lessThan()` so the list sorts by what the
  number means, not by its characters.
- §8.3: an `Issue Date` column, using data already stored since migration
  003 — no schema change, just a new column.
- Certificate list column order is now **No. · Name · Issue Date · Expiry
  Date · Category**, default-sorted by No. ascending, with blank numbers
  sorting after every certificate that has one (so the ones the fleet
  refers to by number stay together). Clicking any header still re-sorts
  by that column.
- 190 unit tests total, across 15 suites (was 142/13) — five new (a new
  `tst_VesselSelectorRefresh` suite covering the selector defect, a new
  `tst_CertificateListNumber` suite testing the domain rule directly, plus
  additions to `tst_CertificateRepositoryValidation` and
  `tst_CertificateListWidget`).

Verified on screen, not just in unit tests (database backed up first, then
restored): the list actually renders `3A`, `9`, `15D`, then a blank row in
that order — the exact case plain-text sorting would have gotten wrong; the
form's validator refused a hyphen and a digit typed after a letter, live,
as keystrokes; and migration 004 applied cleanly to a database that already
had 001–003 on it, confirming 003 was never touched and `ALTER TABLE ...
CHECK` works against a populated table.

Judgment calls made and reviewed:
- The migration's `CHECK` constraint (GLOB-based) and the domain's
  `isValid()` (regex-based) encode the same rule two different ways, since
  SQLite has no regex support — a place the two could in principle drift.
  Tests pin both sides independently, and both were checked against the
  same set of examples during review; no drift found.
- Letters compare case-insensitively (`3a` and `3A` are the same
  certificate to a person), with a case-sensitive tiebreak only so the sort
  is stable. The spec didn't say; the validator permits both cases, so a
  rule was needed.
- Blank-last only holds under the default ascending sort; clicking the
  header to sort descending puts blanks first, because Qt's table sort
  reverses `operator<`. This matches what §8.2 actually specifies (the
  *default* sort) rather than a stronger "always last" rule, which would
  need custom sort handling not asked for.
- The certificate row's id — used to know which row a double-click is
  editing — moved from the Name cell to the new No. cell, since it rides on
  column 0 and column 0 is now No. Invisible to the user either way.

One minor note, not a defect: `MainWindow.cpp`'s new
`refreshVesselSelector()` uses `QSignalBlocker` without an explicit
`#include <QSignalBlocker>` — it compiles today because another Qt header
pulls it in transitively, but that's not guaranteed across Qt versions or
compilers. Worth adding the explicit include next time this file is
touched; not worth a separate pass for on its own.

---

## Decisions confirmed by the developer

- `schema_version` **carries the full column set from `CLAUDE.md` §6.5**,
  including `origin_node` (`'LOCAL'`) and `revision` (always `1`) —
  correcting an earlier note here that said it was exempt. It never actually
  was; the table has always required both. See "Outstanding technical debt"
  below if the exemption is still wanted later.
- The `singleton` column on `installation` **stays** — the database enforces
  the single-row rule rather than the application remembering it.
- `created_by = 'SYSTEM'` for migration records until a user system exists.
- SQL comments being stripped from the copy SQLite stores is accepted; the
  files on disk are untouched.
- Certificates belong to a **vessel**. There is no equipment registry anywhere
  in this project. Equipment certificates are ordinary certificates with a
  free-text `applies_to` field.
- Annual and intermediate surveys are **parallel tracks** by default
  (`intermediate_mode = 'ADDITIONAL'`), per the developer's reading of HSSC.
  `REPLACES_ANNUAL` is available per certificate type for the cases that
  substitute, such as the Cargo Ship Safety Equipment periodical survey.
- An overdue survey invalidates the certificate regardless of its expiry date.
- CSV before `.xlsx`. No new dependency until the simple version proves
  insufficient.
- Migration 002 creates the `vessel` table during step 3, resolving the
  ordering problem where the wizard needed a vessel to point at before step 4
  created the table.
- IMO numbers are validated by check digit at every entry point, and stored
  as TEXT.
- Installation mode is permanent. No change UI, and no password — that would
  be a source-code constant with no user system behind it. Becomes an
  Administrator action at step 15. Until then, the recovery path for a wrong
  choice is to delete the database file and relaunch, which is safe only
  because the wizard runs before anything can write.
- The mode is displayed permanently in the UI so a wrong choice is noticed
  immediately rather than after data entry.
- The IMO check-digit validator lives in `core/ImoNumberValidator`, not under
  a module's `domain/` folder, because both the wizard (core) and the future
  Vessel CRUD form need it without either depending on the other.
- `0000000` passes the check-digit formula (every digit zero sums to zero).
  Left as-is deliberately — the formula stays pure with no special-casing
  for implausible-looking numbers, and a unit test pins the behaviour so it
  can only change on purpose.
- `installation.vessel_id` → `vessel.id` is enforced by the write
  transaction, not by a database foreign key — migration 001 never declared
  one, and SQLite can't add a constraint to an existing table without a
  rebuild. Not worth doing for a single-writer local file; the transaction
  already prevents an orphaned row. `docs/first-run-wizard-spec.md` §5 has
  been corrected to describe this accurately.
- The vessel screens split by installation mode rather than sharing one
  widget: `VesselListWidget` (OFFICE) and `VesselDetailPage` (VESSEL) are
  separate classes built around the same `VesselEditForm`, instead of one
  screen that hides Add/Delete when in VESSEL mode.
- Removing a vessel from the fleet is out of scope until a module that
  depends on vessels exists to design the rule against (see
  `docs/vessel-crud-spec.md` §2).
- `CertificateCategory::Unset` is a real enum value, approved as a
  deliberate exception to "no sentinel values" — it exists purely so
  `create()`/`update()` can reject a certificate with no category chosen,
  which a plain three-value enum can't represent. It's never written to the
  database (`categoryToCode()` returns an empty string for it, on purpose)
  and every switch over `CertificateCategory` in the codebase already
  handles it explicitly. The distinction that matters: this is a
  UI/validation state, not a fourth kind of certificate.
- No foreign key on `certificate.vessel_id` → `vessel.id`. Same trade-off as
  `installation.vessel_id` in step 3, and resolved the same way for
  consistency: nothing in this schema uses a `REFERENCES` clause yet, every
  repository already re-validates its own scope and existence checks
  defensively (`checkVesselAllowed()`), and soft deletes mean rows are never
  actually removed, so the orphan risk an FK guards against barely exists
  today. The trade-off worth remembering: a real FK would catch a bad
  `vessel_id` from a future non-UI write path — a CSV import (step 11) is
  the concrete example — at the database layer for free, without relying on
  every future writer remembering to call the repository. Revisit this when
  step 11 (CSV import) or the sync engine is designed, since those are the
  first paths that write outside the repository's own validation.
- The "No." field (`list_number`) is a single free-typed box with a live
  validator, not two separate boxes for number and letter — tried, rejected
  by the developer as fiddly to fill in during data entry. The format
  restriction (digits, then letters) is what makes a single box safe: it
  guarantees the value can always be split unambiguously for sorting.
- The certificate list's default sort changed from Name to `list_number`
  ascending. This matters to the developer beyond convenience — the fleet
  refers to certificates by this number in day-to-day communication (e.g.
  "Certificate 15D") specifically so the full name doesn't have to be
  typed out, so the list needs to read in that order without the user
  having to click anything.

## Outstanding technical debt

- No SQLite viewer installed on the machine. DB Browser for SQLite is the
  suggested tool — a viewer the developer runs, not a project dependency.
- Qt Creator breakpoints not working (see Environment above).
- `schema_version` requires `origin_node` and `revision` even though it's
  local bookkeeping that never replicates (see the corrected decision
  above). Actually exempting it needs a table rebuild, not a plain `ALTER
  TABLE` — SQLite migrations can't drop `NOT NULL` columns in place. Low
  priority: the current state is stricter than necessary, not looser, so
  nothing is broken by leaving it. Worth a dedicated migration if it starts
  to matter.
- `core/VesselRepository.cpp` is 312 lines, over the ~300-line guideline in
  `CLAUDE.md` §9 (step 4's own report said 280 — verified against the actual
  file, which is 312). Not split now: its six methods are cleanly separated
  and there's no single obvious seam to extract, unlike the statement
  splitter pulled out of `MigrationRunner`. Watch it — if it grows again in
  a later step, that's the moment to pull out something real.
- `modules/certificates/data/CertificateRepository.cpp` is now ~310 lines,
  over the same guideline, having grown past it while adding the `No.`
  field's handling. Same call as `VesselRepository.cpp`: no obvious seam,
  not split now, but this is the file step 6 (endorsements) will touch
  next, so it's the one to watch most closely.
- `tests/modules/certificates/tst_CertificateRepositoryValidation.cpp` is
  now ~301 lines, also just over the guideline, for the same reason (the
  new `list_number` validation tests). Same call: not split now.
- The first-run wizard (step 3) still lets a duplicate IMO number fail with
  the raw SQLite constraint text, rather than the friendly pre-check
  `VesselRepository::create()`/`update()` now do (step 4). Worth
  backporting next time that file is touched.
- `MainWindow.cpp` uses `QSignalBlocker` without including
  `<QSignalBlocker>` directly — works today via a transitive include, but
  is fragile. Add the explicit include next time this file is touched.

---

## Architecture decision: no `certificate_type` table

Reached while designing what was going to be step 5. The original plan (and
the original `certificate-control-spec.md` §5) had a shared `certificate_type`
catalogue — survey rules (`requires_annual_survey`, `intermediate_mode`,
etc.) looked up from a fixed set of types, seeded on install.

Rejected, on the developer's correction: different vessels can legitimately
need different rules for what looks like "the same" certificate — a
short-term certificate on one ship, a full-term one on another, issued by
different authorities. Nothing about a certificate's rules is safe to assume
is shared. There is also no reliable way for this project to seed an
accurate catalogue against the IMO Compendium on the developer's behalf —
attempting one surfaced a real ambiguity in the source list (`certificate-control-spec.md`
§2 lists both "ISPP" and "Sewage" as if they were different statutory
certificates; they may be the same one) that's exactly the kind of mistake a
seeded, hard-to-correct migration should not be allowed to bake in.

Resolved by removing `certificate_type` entirely. Every certificate now
carries its own `name`, `category`, and survey-rule fields directly,
entered by whoever adds it. This also resolved the "no expiry" question
(some certificates, a Tonnage Certificate typically, never expire and have
no survey requirement at all) — `expiry_date` is simply nullable on
`certificate`, with `computeCertificateState()` returning `Valid`/`NotRequired`
immediately when it's null, and rejecting a null expiry paired with either
survey flag as invalid input, since there's no anniversary to schedule
against without an expiry date.

Consequence for the build order: what was step 5 ("Certificate type
catalogue + seed data") is gone. Step 5 is now Certificate CRUD directly
— what used to be step 6 — and every step after it shifted up by one.
`CLAUDE.md` §5 and §11, and `certificate-control-spec.md` §3.4, §4, and §5,
were all updated to match.

## Next step

**Step 6 — Endorsement model and `computeCertificateState()` + unit tests.**
Certificates can now be entered and edited, but nothing yet reads their
survey rules to say whether one is valid, due, or overdue — that logic is
`computeCertificateState()`, specified in
`docs/certificate-control-spec.md` §4, and it hasn't been built yet, only
designed. This step also introduces the `endorsement` table: the record of
each survey actually being carried out, which is what
`computeCertificateState()` needs to check against to know whether an
intermediate survey window has been met.

The developer has already asked for two things the list should eventually
show — "Survey from / Survey to" and "Days left" / a status colour — and
both map directly onto what `certificate-control-spec.md` §3.2 and §4
already designed (`windowOpens`/`windowCloses` per survey, and the
days-left and status-colour rules in §4.3/§4.4). That wiring is step 7, once
step 6's engine exists to wire up.

No implementation spec written yet for step 6 — bring it up here for a
design discussion first, the same way steps 3, 4 and 5 went. Two things
worth deciding early: what an endorsement needs to record (survey date,
surveyor/class society, place — anything else?), and whether an endorsement
belongs to `modules/certificates/domain` alongside `Certificate`, or gets
its own repository the way `Certificate` did.

Full build order is in `CLAUDE.md` §11.
