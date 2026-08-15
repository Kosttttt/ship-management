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

A related PowerShell gotcha, hit for the first time in step 6: PowerShell
does not escape double quotes when handing a string to a native
executable, so a commit message containing a quoted phrase (e.g. `"not
before issue"`) gets split into separate arguments and `git commit -m`
fails with a `pathspec ... did not match any file(s)` error — nothing gets
committed, but it's a confusing error to hit. Writing the message to a
temporary file and using `git commit -F <file>` avoids the whole class of
quoting problem; delete the temp file afterward.

A third one, found the hard way in step 7 (see that step's writeup for the
full incident): `%APPDATA%\Ship Management\Ship Management System\` — the
path this file's own table above lists as "App data folder" — and the
folder the running application actually reads from are **not always the
same file** on this machine. The app resolves its data folder via
`QStandardPaths::AppDataLocation`, and something about how it's currently
launched routes that to a virtualized
`AppData\Local\Packages\...\LocalCache\Roaming\...` path instead of the
plain `%APPDATA%` path a PowerShell command sees. **Never run a delete or
overwrite command against either path** — see `CLAUDE.md` §10.1 for the
full rule. If the actual data folder in use ever needs confirming, log it
at startup rather than assuming which of the two it is.

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

### Step 6 — Endorsement model & `computeCertificateState()` ✅
Full design in `docs/certificate-endorsement-spec.md`, written after a design
discussion covering five open questions — endorsement fields, where the
model lives, deferring extensions, hardcoding `AlertThresholds` until
Settings exists, and a correction to the late-endorsement matching rule
(below). Built and independently verified file-by-file against the spec —
migration, every domain file, the repository, both new UI classes,
`CertificateEditDialog`'s wiring, `CertificatesModule`, `CertificateListWidget`,
`CMakeLists.txt`, and all new/changed test files — not just from Claude
Code's own report. That verification caught a real, build-breaking gap; see
below.

- `migrations/005_create_endorsement.sql` — the `endorsement` table.
  `survey_type` is `NOT NULL` (not nullable) with a 4-code CHECK
  (`INITIAL`/`ANNUAL`/`INTERMEDIATE`/`RENEWAL`), the same audit columns as
  everywhere else, no foreign key to `certificate` — same trade-off as
  `certificate.vessel_id`, resolved the same way for consistency.
- `modules/certificates/domain/Endorsement` — a `SurveyType` enum with an
  `Unset` sentinel (mirrors `CertificateCategory::Unset`), the `Endorsement`
  struct, and a `SurveyTypeCodes` namespace mirroring `CertificateCodes`.
- `modules/certificates/domain/CertificateState` — the pure function at the
  heart of this step. `computeCertificateState(certificate, endorsements,
  thresholds, today)` takes `today` as a parameter rather than reading
  `QDate::currentDate()` itself, which is what makes every rule in
  `certificate-control-spec.md` §4 unit-testable without mocking the clock.
  Returns `ExpirySeverity`, `SurveySeverity`, a combined `DisplayStatus`,
  `isValid`, `daysLeft` (negative once overdue), the next outstanding
  survey and its window, and a human-readable `reason` string (including a
  note when a survey was completed late).
- `modules/certificates/domain/SurveySchedule` — split out of
  `CertificateState.cpp` once that file reached 349 lines and a genuine
  seam appeared: window/anniversary arithmetic is a distinct concern from
  severity/display logic. Both files now sit comfortably under the
  ~300-line guideline. Contains the anniversary and window calculations and
  the two-pointer greedy matching walk described below.
- `modules/certificates/data/EndorsementRepository` — mirrors
  `CertificateRepository`'s shape. Since `endorsement` has no `vessel_id` of
  its own, VESSEL-mode scope is applied *transitively*: a SQL `JOIN`
  against `certificate` in `list()`, and a `loadParentCertificate()` lookup
  in `create()` that does double duty — it enforces scope and supplies the
  certificate's `issue_date` for the "endorsement can't predate the
  certificate" check, in one query. Add-only this step, no `update()`/
  `delete()` — left open deliberately, since how a correction to a
  compliance record should work is a real design question nothing yet
  depends on.
- `modules/certificates/ui/EndorsementEditForm` / `EndorsementEditDialog` —
  the add screen. When a certificate requires only one kind of survey, the
  type shows as a fixed label with no combo box at all — one option is not
  a choice. When it requires both, the combo only ever offers Annual/
  Intermediate; `INITIAL`/`RENEWAL` are storable in the schema but never
  offered here.
- `modules/certificates/ui/CertificateEditDialog` gained an Endorsements
  section (a table + "Add Endorsement" button), shown only when editing an
  existing certificate that requires at least one survey type. That
  visibility decision is made once, from the certificate as saved, not
  live from the checkboxes as they're being edited — a requirement just
  ticked but not yet saved has no certificate id for an endorsement to
  point at.
- `CertificatesModule` and `CertificateListWidget` now thread an
  `EndorsementRepository` through to `CertificateEditDialog`, alongside the
  existing `CertificateRepository`.
- 245 unit tests total, across 19 suites (was 190/15) — four new suites:
  `tst_CertificateStateExpiry` and `tst_CertificateStateSurvey` (the
  pure-function tests, covering every edge case in
  `certificate-control-spec.md` §4.8 — leap-day anniversary clamping,
  30-day-month window clamping, short-term certificates with no
  anniversaries at all, expiry and survey both being wrong at once,
  REPLACES_ANNUAL vs ADDITIONAL intermediate modes, and the `daysLeft` sign
  flip), `tst_EndorsementRepository` (scope, validation, audit columns),
  and `tst_EndorsementUi` (section visibility, fixed-label-vs-combo
  behaviour).

**The late-endorsement matching rule** — a correction made to the spec,
not a defect in the build. `certificate-control-spec.md` §4.5's original
pseudocode required an endorsement to fall *within* its survey window to
satisfy it, which read literally meant a certificate that ever missed a
window would show Overdue forever, with no way back to valid even after
the missed survey actually happened. Caught and fixed in the spec before
this step was built: an endorsement now satisfies the earliest unsatisfied
anniversary whose window it falls on or after the opening of, with no
upper bound at the window's close — a late survey is still a survey, just
recorded as late in the `reason` string. Implemented in
`SurveySchedule::evaluateAnnual()`/`evaluateIntermediate()` as a
two-pointer greedy walk over date-sorted anniversaries and endorsements;
hand-traced against several examples during review, and covered directly
by `aSurveyCompletedAfterItsWindowClosedStillSatisfiesIt` /
`reasonRecordsThatASurveyWasLate` /
`eachAnniversaryClaimsTheEarliestEndorsementAvailableToIt` in
`tst_CertificateStateSurvey.cpp`.

Judgment calls made (Claude Code's, all reviewed and confirmed rather than
changed):
1. `daysLeft` goes negative once a date has passed, rather than clamping at
   zero — the natural signed meaning, pinned by a test.
2. Intermediate survey requires ≥3 anniversaries to exist at all (i.e. a
   ≥4-year certificate term) — a shorter term has nowhere to anchor the
   window, so it's reported as not required rather than inventing one.
3. An endorsement dated before the first window ever opens satisfies
   nothing — the correct reading of "on or after the opening," not a bug.
4. The Endorsements section's visibility is decided once at dialog
   construction, not live — see above.
5. A certificate with no expiry but with survey flags ticked still gets a
   sane answer from `computeCertificateState()` (`NotRequired`/`Valid`)
   even though the repository should already block that combination —
   cheap defense in depth, not dead code.
6. The `reason` string is assembled in the domain layer, not the UI, so
   alerts/tooltips/reports all describe a certificate's status identically
   without duplicating the logic.

**A verification episode worth recording**, since it nearly produced a
wrong entry in this file. Claude Code's first report of this step (245/19
tests passing, clean build) did **not** reflect the actual working tree at
`D:\dev\ship-management`: `CertificatesModule` and `CertificateListWidget`
were never updated to thread `EndorsementRepository` through to
`CertificateEditDialog`, so the code as it stood on disk would not
compile. This was caught only by reading the actual files via the device
bridge rather than trusting the summary — exactly the verification step
this project's process already calls for, and the reason it's worth
calling for. It took several rounds to pin down cleanly (a stale
device-bridge cache on the reviewing side briefly pointed suspicion the
wrong way too, before a plain visual check of the file, opened fresh from
the real project tree in Qt Creator, settled it) — but the fix itself,
once correctly identified, was small: four files, each needing
`EndorsementRepository` threaded through one more layer. Confirmed fixed
and re-verified line-by-line afterward, then confirmed again with a
genuinely clean rebuild (deleted build folder, reconfigured, built, ran
`ctest`) with the raw pass/fail output read directly rather than
summarized: 100% tests passed, 0 failed, 19 of 19.
**Takeaway for future steps: always ask for the raw build/test output, not
a description of it, and don't treat "I built and it passed" as verified
until the actual files backing that claim have been read.**

Committed as `1695141` and pushed to `origin/main`.

### Step 7 — Certificate list screen: status colours, filters, days-left ✅
Full design in `docs/certificate-list-status-spec.md`, written after a design
discussion that settled the column layout, the status colour/label table (the
developer's own scheme, overriding an earlier draft that grouped some
statuses together), the Survey From/To blank rules, and the severity-based
sort for the new Status column. Built and independently verified
file-by-file against the spec — `StatusItem`, the rewritten
`CertificateListWidget`, both CMake files, and the new test suite — not just
from Claude Code's own report.

- `modules/certificates/ui/StatusItem` — the seventh `QTableWidgetItem`
  subclass pattern in this project (after `ListNumberItem`): a label and
  background/foreground colour per `DisplayStatus`, sorted by severity via
  an override of `operator<` rather than alphabetically. The severity rank
  is simply `static_cast<int>(status)`, since `DisplayStatus` is already
  declared worst-last in the domain layer — commented in the code as
  load-bearing, since reordering that enum would silently reorder this
  column.
- `modules/certificates/ui/CertificateListWidget` — rebuilt around the new
  seven-column layout (`No. · Name · Status · Expiry Date · Survey From ·
  Survey To · Days Left`; `Issue Date` and `Category` moved out, still on
  the edit dialog). `reload()` now calls `computeCertificateState()` once
  per certificate, reading `QDate::currentDate()` exactly once per reload
  and handing the same value to every row — the one legitimate place
  "today" is read in this whole call chain, since the domain function
  itself never reads the clock. A new "Show only certificates needing
  attention" checkbox filters in memory after computing state for every
  row (severity is a derived value, never stored, so there's nothing to
  filter on directly in SQL).
- Colour table exactly as specified by the developer: red for Expired,
  Survey Overdue, and Critical; orange for Expiring Soon; yellow for Survey
  Due; light green for Due Soon; Valid carries no background at all rather
  than a literal white fill, so an unremarkable certificate stays visually
  unremarkable and the screen doesn't need to change if the app ever gets
  a dark theme.
- 259 unit tests total, across 20 suites (was 245/19) — one new suite,
  `tst_CertificateListStatus`, covering all nine edge cases from spec §9
  plus three supporting tests (the filter keeping the right rows, Valid
  rows carrying no highlight, urgent rows carrying the exact specified
  colour values). The pre-existing column-layout test in
  `tst_CertificateListWidget.cpp` was updated in place for the new
  seven-column header row.

Judgment calls made (Claude Code's, all reasonable, no changes needed):
1. `Days Left` is stored via `Qt::DisplayRole` as an `int`, not as text, so
   the column sorts numerically (`-7` before `10`) rather than by string
   comparison. Displays identically either way; only the sort behaves
   correctly this way.
2. The test suite seeds every certificate's dates relative to
   `QDate::currentDate()` rather than fixed calendar dates, since the
   widget reads the clock itself (by design — the screen is the one place
   that's supposed to) and a test has no way to inject a fake "today" into
   it. This keeps every expected status stable no matter which day the
   suite actually runs.
3. Header-click sorting is tested via `table->sortByColumn()` directly —
   what a real click ultimately calls — rather than synthesising a mouse
   click at a computed header pixel offset.

**A data-loss incident during this step, unrelated to the code above.**
While taking a screenshot to visually confirm the colours, a PowerShell
backup-then-delete-then-restore sequence against the application's real
data folder deleted a populated database (two vessels the developer had
entered by hand, "test" and "test 2") and did not actually restore it — the
backup itself was silently taken from the wrong file. Root cause: on this
machine, `%APPDATA%\Ship Management\...` (where a PowerShell command
resolves) and the folder the running application actually reads (a
virtualized `AppData\Local\Packages\...\LocalCache\Roaming\...` path) are
two different files that look like the same path. No source file, test, or
committed work was involved — this was purely a side effect of a manual
verification step gone wrong. The two lost vessels were themselves test
data created earlier in this same project (during step 5's hands-on
verification), not real fleet data, so nothing of lasting value was lost —
but the near-miss was serious enough that a standing rule now exists in
`CLAUDE.md` §10.1: no command may delete or overwrite anything under the
real app-data folder, for any reason, and any manual visual check must run
against a throwaway data directory instead. Reported to the developer
immediately and directly, with the exact mechanism, rather than being
smoothed over or discovered later.

Committed as `33f90b0` and pushed to `origin/main`, alongside this
`PROJECT-STATUS.md` update, the `CLAUDE.md` §10.1 safety rule, and
`docs/certificate-list-status-spec.md`.

### Step 8 — Settings & `app_setting` ✅
Full design in `docs/settings-app-setting-spec.md`, written after a design
discussion that settled four questions: the singleton-table shape (copying
`installation`'s mechanism exactly, rather than a generic key/value table),
enforcing strictly-increasing thresholds with a friendly message ahead of
the raw CHECK constraint, adding the `last_alert_toast_date` column now but
leaving its behaviour to step 9, and an ungated Settings entry in the
sidebar. Built and independently verified file-by-file against the spec —
migration, both core files, the Settings screen, every wiring point
(`CertificateListWidget`, `CertificatesModule`, `MainWindow`, `main.cpp`),
both `CMakeLists.txt` files, and all five new/changed test files — not just
from Claude Code's own report. **No defects found.**

- `migrations/006_create_app_setting.sql` — the `app_setting` table. The
  singleton mechanism (`singleton INTEGER NOT NULL DEFAULT 1 UNIQUE CHECK
  (singleton = 1)`) is copied verbatim from `001_create_installation.sql`,
  confirmed byte-for-byte identical in intent. Two CHECK constraints: each
  threshold `> 0`, and the three strictly increasing — a correctness
  requirement, not tidiness, since `computeCertificateState()` tests them in
  that order and an out-of-order set would make a whole severity tier
  unreachable. Unlike `installation`, the single row is seeded by the
  migration itself, not a wizard — there's no decision for a user to make,
  since the defaults are already the right starting values. The seed
  `INSERT` builds a version-4-shaped UUID directly in SQL (`randomblob()`/
  `hex()`/`substr()`), since a migration can't call `QUuid::createUuid()`;
  `origin_node = 'LOCAL'`, matching the precedent `schema_version` already
  set, since this migration runs before the first-run wizard creates an
  `installation` row.
- `core/AppSetting` — a plain struct (`id`, three `int` thresholds default
  30/60/90, a nullable `lastAlertToastDate`), deliberately free of any
  dependency on the certificates module and of audit columns, matching the
  convention `Vessel`/`Certificate` already follow (the repository owns
  audit columns, not the struct).
- `core/AppSettingRepository` — `read()`/`update()`, no `create()` (the row
  always exists once migration 006 has run), no `InstallationContext` and no
  scope filtering (app settings are installation-wide, not vessel-specific).
  `validate()` checks positivity then strict ordering, both with named-pair
  messages (e.g. "Critical (%1 days) must be fewer than Expiring Soon (%2
  days)."), before the raw CHECK constraint is ever reached. `update()`
  deliberately never writes `last_alert_toast_date` — nothing owns that
  column until step 9, and a threshold save must not silently clear it.
- `app/SettingsPage` — a plain form, no dialog wrapper, editing the one
  settings row directly (the same shape `VesselDetailPage` uses for the one
  vessel in VESSEL mode). Three `QSpinBox` fields (range 1–3650 days) whose
  Save button is disabled live whenever the ordering rule is broken, with a
  hint label naming the offending pair — the same message the repository's
  own `validate()` would give, so the invalid combination is never actually
  something a user can submit. Loads via `read()`, saves via `update()`,
  reloads from the stored row afterward rather than trusting the form.
- Wiring: `main.cpp` constructs one `AppSettingRepository` alongside
  `VesselRepository`/`ModuleRegistry`, before the window, so it outlives
  every screen holding a reference to it — passed into `MainWindow`, which
  threads it into both `SettingsPage` and (via `CertificatesModule`)
  `CertificateListWidget`. `CertificateListWidget::reload()` is the one
  place an `AppSetting` (core) becomes an `AlertThresholds` (this module),
  translated a field at a time on the module's side of the architecture
  boundary — core knows nothing about `AlertThresholds`. A failed read
  leaves the hardcoded 30/60/90 defaults in place, the same "something
  reasonable beats nothing" call step 7 made for unreadable endorsements.
- "Settings" is a fixed sidebar entry, core rather than a module, placed
  **last** — after "Vessels" and every registered module — and visible in
  both installation modes with no role gating, since roles are layered on
  at the end of the project (`CLAUDE.md`'s development approach), not
  designed around now.
- 286 unit tests total, across 22 suites (was 259/20) — two new suites
  (`tst_AppSettingRepository` covering spec §8 items 1–6: seeded defaults,
  both ordering refusals with named-pair messages, zero/negative refusal,
  a valid update round-tripping and bumping revision, and the raw CHECK
  text never reaching the user; `tst_SettingsPage` covering item 7: the
  Save button's live validation, re-enabling once corrected, and a save
  actually writing through to the repository), plus two new cases added to
  `tst_CertificateListStatus` (items 8–9: an edited threshold changing what
  the list shows for the same certificate; a `read()` failure falling back
  to the hardcoded defaults rather than breaking the screen) and
  constructor-signature updates to `tst_ModuleRegistry` (a new
  `settingsEntryIsPresentInVesselModeToo` case, plus sidebar-count
  assertions updated for the third fixed entry) and
  `tst_VesselSelectorRefresh` (all `MainWindow` construction sites updated
  for the new `AppSettingRepository&` parameter).

Judgment calls made (Claude Code's, all reviewed individually):
1. The seed `INSERT`'s UUID, built from SQLite's own randomness rather than
   a fixed literal — confirmed correct: a literal would give every
   installation in the fleet the same settings-row id, which defeats the
   point of per-installation UUIDs.
2. `origin_node = 'LOCAL'` for the seeded row — confirmed correct, matching
   the precedent `schema_version` already set for the same reason (no
   `installation` row exists yet when this migration runs).
3. `AppSetting` carrying no audit fields, flagged as a literal-vs-convention
   interpretation call — confirmed correct: this matches the established
   `Vessel`/`Certificate` convention exactly (repository owns audit
   columns, domain struct doesn't), not a deviation from it.
4. `update()` never writing `last_alert_toast_date` — confirmed correct and
   load-bearing: without this, the first threshold save after step 9 ships
   would silently reset "have I shown today's toast" back to never-shown.
5. Settings placed last in the sidebar — confirmed correct per spec §6: a
   utility screen rather than somewhere work happens, so it belongs after
   the modules, not before or among them.
6. The certificate list not live-refreshing its already-rendered rows when
   Settings is saved on another screen, flagged as a possible follow-up —
   **accepted as shipped, not a defect.** The data itself is never wrong,
   only the on-screen severity colouring until the next `reload()` trigger
   (switching vessels, toggling the filter, or closing the add/edit
   dialog); the Settings screen's own confirmation text already sets that
   expectation ("Saved. The certificate list uses these the next time it
   loads."). Revisit only if step 9's alert engine ends up needing a
   cross-screen "settings changed" signal for its own reasons — the
   certificate list could piggyback on that rather than this screen
   inventing its own `vesselsChanged()`-style signal just for itself.

One minor, non-blocking nit found during verification, not a defect: two
comments — `CertificatesModule::alertProviders()` and a comment in
`tst_ModuleRegistry.cpp` — still say "the badge arrives in step 8," left
over from before step 6's build-order renumbering moved Alerts to step 9.
Purely cosmetic; worth a one-line fix next time either file is touched, not
worth a separate pass.

Committed as `a73a8dd` and pushed to `origin/main` (`ea88f23..a73a8dd`),
alongside this `PROJECT-STATUS.md` update and `docs/settings-app-setting-spec.md`.

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
- Endorsements are add-only for now; editing or deleting a recorded survey
  is an open design question, deferred until something depends on it.
- Extensions have no table and no parameter in `computeCertificateState()`
  yet — deferred to a dedicated build-order step (11, after Renewal)
  rather than half-building them alongside endorsements.
- `AlertThresholds` ships with hardcoded 30/60/90-day defaults; every call
  site takes it as a parameter, never a constant, specifically so a real
  Settings screen (build-order step 8, before Alerts) can make them
  editable without touching `computeCertificateState()` itself.
- `INITIAL` and `RENEWAL` are valid, storable survey types, but
  `computeCertificateState()` never matches an endorsement of either type
  against a window — only `ANNUAL` and `INTERMEDIATE` drive the
  anniversary calculation.
- The build order (`CLAUDE.md` §11) grew from 13 to 15 steps during step
  6's design discussion: "Settings & `app_setting`" inserted as step 8
  (before Alerts), "Extensions" inserted as step 11 (after Renewal),
  everything after renumbered accordingly.
- The certificate list's status colour scheme is the developer's own,
  overriding an earlier draft that grouped Expiring Soon/Survey Due/Survey
  Overdue away as separate labels: all seven `DisplayStatus` values stay
  visually distinct as text, with red covering three of them (Expired,
  Survey Overdue, Critical) rather than red being reserved for only the
  most severe. Reasoning confirmed directly: severity should escalate
  through white → light green → yellow → orange → red, and Critical
  belongs at the same visual urgency as an already-broken certificate
  since it means action is needed right now, even though the certificate
  itself hasn't failed yet.
- No commands that delete or overwrite the application's real data folder
  may run for any reason, including manual visual verification — see
  `CLAUDE.md` §10.1. A throwaway data directory is required for any manual
  run of the app used to look at something rather than to keep the data.
- `app_setting` is a fixed-column singleton table, the same shape as
  `installation`, not a generic key/value table — simpler to read, at the
  cost of a migration to add each future setting. With exactly one thing to
  store today, that trade-off favours simplicity.
- The three alert thresholds must be strictly increasing
  (`criticalDays < expiringSoonDays < dueSoonDays`), enforced with a
  friendly named-pair message ahead of the raw database CHECK constraint —
  not just validated for positivity alone.
- `AppSetting`/`AppSettingRepository` are core-owned and carry zero
  dependency on the certificates module; the one-line-per-field translation
  into the module's own `AlertThresholds` happens inside
  `CertificateListWidget::reload()`, on the module's side of the
  architecture boundary, keeping `CLAUDE.md` §4's "dependencies point
  downward only" rule intact without touching any already-tested step 6
  code.
- Settings is visible in both installation modes with no role gating —
  consistent with the project's stated approach of building role-based
  access last, on top of already-working functionality, rather than
  designing around it from the start.

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
- `modules/certificates/data/CertificateRepository.cpp` (~310 lines) and
  `tests/modules/certificates/tst_CertificateRepositoryValidation.cpp`
  (~301 lines) are both still over the ~300-line guideline, unchanged
  since step 5 — step 6 touched neither. Still no obvious seam; still
  watching, not splitting.
- Step 6's own new files stayed well under the guideline, including after
  the `CertificateState.cpp`/`SurveySchedule.cpp` split (each ~175–180
  lines) — no new size debt from this step.
- Step 7's new/changed files are all well under the guideline too
  (`StatusItem.cpp` is under 90 lines; `CertificateListWidget.cpp` grew but
  stayed under 300). No new size debt from this step either.
- The exact mechanism behind the `%APPDATA%` vs. virtualized-path split
  described in the Environment section above is not fully understood — only
  that it exists and is dangerous to assume around. Worth investigating
  properly (rather than just avoiding) if it ever blocks something more
  than a manual visual check, e.g. if a future backup/restore feature needs
  to know the real path reliably.
- Two stale comments (`CertificatesModule::alertProviders()` and
  `tst_ModuleRegistry.cpp`) still say "the badge arrives in step 8" from
  before step 6's build-order renumbering moved Alerts to step 9. Cosmetic
  only — fix inline next time either file is touched.
- The certificate list does not live-refresh its already-rendered rows when
  thresholds are changed on the Settings screen without an intervening
  `reload()` trigger. Accepted as shipped (see step 8's judgment call 6
  above); revisit only if step 9's alert engine needs a cross-screen
  "settings changed" signal for its own reasons, in which case the
  certificate list can piggyback on it.
- Step 8's own new/changed files stayed comfortably under the ~300-line
  guideline (`AppSettingRepository.cpp` ~140 lines, `SettingsPage.cpp` ~170
  lines) — no new size debt from this step.

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

**Step 9 — Alerts.** The certificate list now computes and displays live
status, and the alert thresholds behind it are finally editable rather than
hardcoded — step 8 built exactly the plumbing this step needs
(`app_setting.last_alert_toast_date`, unused since step 8, is waiting for
this step to read and write it). Per `CLAUDE.md` §11, this step adds:

- A sidebar badge (or similar) showing how many certificates currently need
  attention, using the same `computeCertificateState()`/`AlertThresholds`
  machinery the certificate list already uses — no new severity logic, just
  a new consumer of it.
- A once-per-day toast/notification summarising what needs attention,
  tracked via `app_setting.last_alert_toast_date` so it fires at most once
  per calendar day per installation.
- A filtered drill-down from the badge/toast straight to the certificate
  list's existing "needs attention" filter, rather than a separate screen
  duplicating what that filter already shows.

No implementation spec written yet for step 9 — bring it up here for a
design discussion first, the same way steps 3–8 went. Worth deciding early:
where the badge lives in the UI (sidebar item text, a small icon overlay,
or something else), whether the toast is a native OS notification or an
in-app one, what "needs attention" means for the badge/toast specifically
(the same `DisplayStatus != Valid` rule the list's filter already uses, or
something narrower), and whether the badge count should be per-vessel in
OFFICE mode or fleet-wide.

Full build order is in `CLAUDE.md` §11.
