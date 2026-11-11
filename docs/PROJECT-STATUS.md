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

## Producing a Release build

Not a build-order step — tooling, done once the developer wanted an app
that opens by double-clicking rather than through Qt Creator every time.

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/mingw_64"
cmake --build build-release
```

The target CMake produces is `ShipManagement` (declared at `CMakeLists.txt`
line 149, top level — not under `src/app/`), built to
`build-release\ShipManagement.exe`.

`dist\ShipManagement\` is where the standalone, shareable copy lives —
**not** `build-release\` itself, which also holds all 24 test executables.
`dist\` is a plain copy, not a build output: rebuilding `build-release\`
does **not** update it automatically. Before every redeploy, copy the
freshly built exe over the one already in `dist\ShipManagement\`, *then*
run:

```powershell
windeployqt --release --compiler-runtime dist\ShipManagement\ShipManagement.exe
```

This was missed once already — a redeploy that skipped the copy would have
bundled fresh DLLs around a stale exe, with a perfectly clean build log
giving no hint anything was wrong.

The console window is gated by build config in `CMakeLists.txt`
(`WIN32_EXECUTABLE`/`MACOSX_BUNDLE` set to `$<CONFIG:Release>`): Debug
keeps it, with `qInfo()`/`qDebug()` visible live; a Release build hides it
entirely, so diagnosing a Release build means reading the rotating log file
under `%APPDATA%\Ship Management\Ship Management System\logs` instead —
that file captures everything regardless of which build produced it.

`build-release/` and `dist/` are both in `.gitignore`, alongside the
pre-existing `build/` — added specifically because this project's `git add
-A` commit habit would otherwise have staged the entire Release build and
~40 Qt DLLs into git history on the next commit.

The console-window gate and the `.gitignore` update (plus
`docs/manual-test-checklist.md`, which had been sitting uncommitted since
it was written) were committed as `281ec62` and pushed to `origin/main`
(`6cb55f9..281ec62`).

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

### Step 9 — Alerts ✅
Full design in `docs/alerts-spec.md`, written after a design discussion that
settled the badge phrasing (text in the sidebar label, not a painted icon
overlay), the banner medium (in-app, not a native OS toast — cross-platform
consistency over polish), the severity rule (identical to the certificate
list's own "needs attention" filter, so the two can never disagree), and —
after an early version of the plan turned out to have a real gap — a
per-vessel banner design: one row per vessel with something outstanding,
each with its own View button, rather than one ambiguous fleet-wide count
with nowhere obvious to land when clicked. Built and independently verified
file-by-file against the spec — both new interface/class pairs, every
wiring point (`MainWindow`, `CertificatesModule`, `CertificateListWidget`,
`AppSettingRepository`), both `CMakeLists.txt` files, and the full new test
suite — not just from Claude Code's own report. **No defects found.**

- `app/AlertProvider.h` — the interface forward-declared since step 5,
  implemented for the first time. `VesselAttentionCount{vesselId,
  vesselName, count}` plus one method, `attentionByVessel()`, returning
  only vessels with something outstanding. Generic by design: nothing
  about it names certificates, so a second module could implement it later
  without a redesign — untested today, since only one module exists.
- `modules/certificates/CertificateAlertProvider` — the certificates
  module's implementation. Walks every vessel `VesselRepository::list()`
  returns (already scope-correct: the whole fleet in OFFICE mode, this
  installation's one vessel in VESSEL mode — nothing extra needed to keep
  a vessel installation from ever seeing another ship's data), and for
  each one counts certificates whose `DisplayStatus != Valid` — the exact
  rule the certificate list's own filter already uses. Same fallback
  contracts as `CertificateListWidget::reload()` throughout: a failed
  threshold read leaves the hardcoded 30/60/90 defaults in place; an
  unreadable endorsement list is treated as "none," not a hard failure.
  Same N+1-per-vessel shape step 7 already accepted for one vessel, now
  run across the whole fleet — not batched, deliberately, at this app's
  target scale.
- `core/AppSettingRepository::recordAlertToastShown()` — a new, narrow
  method that writes only `last_alert_toast_date`, kept deliberately
  separate from `update()` (which still never touches that column, per
  step 8) so a threshold save can never disturb the toast date and a toast
  can never disturb the thresholds.
- `app/AlertBanner` — the dismissible per-vessel panel. One row per
  `(vessel, module)` pair with something outstanding: vessel name, a
  sentence built from the count and the owning module's `displayName()`,
  and a View button. A single dismiss control closes the whole banner. The
  row sentence reads "certificates needing attention: 2" rather than the
  spec's suggested "2 certificates need attention" — Claude Code's own
  call, confirmed correct: the suggested phrasing breaks at a count of 1
  ("1 certificates need attention") with no generic way to singularise an
  arbitrary module's display name, so sidestepping subject-verb agreement
  entirely was the right fix, not a deviation worth reverting.
- `app/MainWindow` wiring: a `QHash<IModule*, int>` remembers which
  sidebar row belongs to which module, populated in the same loop that
  already builds one row per registered module. At startup, after the
  sidebar and every module screen exist: `showDailyAlertBanner()` combines
  every module's `AlertProvider` output, shows the banner and records
  today's date only if something is outstanding **and** it has not
  already been shown today (checked via `AppSettingRepository::read()`,
  with a failed read treated as "never shown," the same fallback contract
  as everywhere else this repository is read); `refreshAlertBadges()`
  rewrites each module's sidebar row with its live count regardless of
  whether the banner showed. A View click (`showAttentionFor()`) sets the
  target screen's "needs attention" filter before switching the vessel
  selector to the row's vessel, so only the filter's own reload and the
  vessel switch's reload happen — no third, redundant one — then switches
  the sidebar to that row. Written generically by `IModule*`, not by
  name, so a second per-vessel module would not require changing this
  method, only adding a sibling to the one `qobject_cast` inside it — the
  same "one piece of direct wiring, generalise once a second module
  exists" precedent `MainWindow` already documents for the vessel
  selector's own link to the certificates screen.
- `CertificateListWidget` gained `setNeedsAttentionFilter(bool)` (the
  banner's drill-down target; a no-op if the filter is already in the
  requested state, since `QCheckBox::setChecked()` only emits `toggled`
  on an actual change) and a new `certificatesChanged()` signal, emitted
  at the end of a successful `reload()`, that `MainWindow` uses to keep
  the sidebar badge live within a session without needing a restart.
- `CertificatesModule` gained a `VesselRepository` and a
  `CertificateAlertProvider` member (declared last, since it holds
  references to the four repositories before it); `alertProviders()`
  (previously `{}`) now returns the one non-owning pointer.
- 23 unit tests suites (was 22) — one new suite, `tst_Alerts.cpp`, covering
  all nine edge cases from spec §9 (nothing outstanding → no banner, no
  badge; one vessel outstanding plus the full drill-down; multiple vessels
  each getting their own row, and acting on one leaving the others alone;
  VESSEL mode seeing only its own vessel, including a second vessel's row
  deliberately left in the database to prove the provider ignores it;
  the once-a-day gate, and the badge staying live independently of it;
  the toast date being left untouched when nothing was actually shown;
  the toast date being recorded the moment the banner is shown, verified
  by constructing a second `MainWindow` in the same session and confirming
  it stays silent; a failed threshold read still producing counts; and the
  badge following `certificatesChanged()` without a restart) — plus the
  now-updated `tst_ModuleRegistry` assertion for `alertProviders()`.

**A process note, self-caught and disclosed, not a defect.** Claude Code's
first "clean rebuild" attempt silently was not clean: an earlier `cd` into
`build` for `ctest` had left the shell's working directory inside the
folder `Remove-Item` was trying to delete, which failed with "being used
by another process" — but because the output was piped, that failure
wasn't obviously visible, and the build that followed looked fine. Caught
by checking "deleted; present: True" rather than trusting the delete had
worked, before pasting anything here. Redone correctly (moved the shell
out first, killed lingering `tst_*.exe` processes, confirmed the folder was
genuinely gone) and the real clean output — 100% tests passed, 0 failed,
23 of 23 — is what's recorded above. Worth noting since this is exactly
the failure shape step 6's own verification episode warned about, caught
this time by the person building it rather than by review after the fact.

Two stale comments flagged as technical debt after step 8 (`CertificatesModule::alertProviders()`
and `tst_ModuleRegistry.cpp` both still saying "the badge arrives in step 8")
are gone as of this step, resolved naturally since both were rewritten for
real. No separate fix needed — removed from the technical-debt list below.

Committed as `09da662` and pushed to `origin/main` (`9735646..09da662`),
alongside this `PROJECT-STATUS.md` update and `docs/alerts-spec.md`.

### Step 10 — Certificate renewal ✅
Full design in `docs/certificate-renewal-spec.md`, written after a design
discussion that corrected the plan this file's own "Next step" section had
sketched. The original assumption — a new certificate row, linked back to
the old one via `certificate.previous_certificate_id` — was wrong: renewal
edits the **same** row in place, no new row is ever created, and that
column stays permanently unused. The discussion also exposed a factual
error of the reviewer's own, not a bug in the code: a claim that survey
windows were calculated from `issue_date` rather than `expiry_date` was
challenged by the developer and found wrong on re-reading
`SurveySchedule.cpp` — the code was already correct and needed no change.
File/document attachment ("new file, old one archived") was confirmed out
of scope, since no file storage exists anywhere in the app yet; deferred to
the future Document Management module. Built and independently verified
file-by-file against the spec — both `EndorsementRepository` files, both
`CertificateEditDialog` files, both `CertificateListWidget` files, the new
test suite, and `tests/CMakeLists.txt` — not just from Claude Code's own
report. **No defects found.**

- `modules/certificates/data/EndorsementRepository::deleteAllForCertificate()`
  — a new soft-delete method, scoped to one certificate's endorsements by
  `certificate_id`. Affecting zero rows is treated as success, not failure
  — a certificate with nothing recorded yet is a normal case, not an error.
- `modules/certificates/ui/CertificateEditDialog` gained a trailing
  `bool isRenewal = false` constructor parameter, guarded as
  `m_isRenewal(isRenewal && existing.has_value())` so renewal mode can never
  be true without an existing certificate to renew. In renewal mode: the
  title becomes "Renew — <name>", the OK button reads "Renew", and the
  Endorsements section is never shown — a stamp added there would belong to
  neither the old cycle (about to be cleared) nor the new one (no
  anniversaries exist until the new dates are saved). A successful save
  also calls `deleteAllForCertificate()`; if that call itself fails, the
  dialog still accepts rather than trapping the user with data already
  written, surfacing a separate warning instead — a sensible call beyond
  what the spec explicitly asked for, confirmed correct on review.
- `modules/certificates/ui/CertificateListWidget` gained a **Renew** button,
  separate from the existing double-click-to-edit action, enabled only when
  a row is selected **and** that certificate actually expires (a
  `kNeverExpiresRole` data flag on the row avoids a second database round
  trip to check). Clicking it loads the certificate's endorsement count,
  asks a confirmation naming what will be lost ("Renew '<name>'?" with
  nothing to lose; "Renewing '<name>' will clear its N recorded survey(s).
  Continue?" otherwise), then opens `CertificateEditDialog` in renewal mode.
  `addCertificate()`/`editCertificate()` both now pass `/*isRenewal=*/false`
  explicitly — see the defect avoided, below.
- Deliberately **not** one database transaction: the certificate update and
  the endorsement-clear are both simple unconditional single-table UPDATEs
  against a local SQLite file, and the bounded consequence of an unlikely
  half-failure (stale endorsements left behind, which the next state
  computation would simply treat as predating the new cycle and ignore)
  didn't justify the extra complexity.
- 24 unit test suites total (was 23) — one new suite, `tst_CertificateRenewal`,
  with 11 test cases: all nine edge cases from spec §7 (successful renewal;
  cancelling the confirmation; cancelling the edit dialog itself;
  Renew disabled with no selection; Renew disabled for a certificate that
  never expires; the confirmation wording with zero vs. some endorsements;
  the Endorsements section never appearing in renewal mode; an end-to-end
  check that a renewed certificate reads Valid afterward; double-click still
  opening the ordinary, non-destructive edit) plus two supporting tests for
  `deleteAllForCertificate()` itself (scoped to the right certificate only;
  zero rows affected is still success).

**A constructor-parameter footgun, caught and fixed by Claude Code before
it shipped, not a defect found on review.** Inserting `isRenewal` before
the existing trailing `QWidget* parent` meant the two pre-existing call
sites (`addCertificate()`, `editCertificate()`), which passed `this`
positionally as the dialog's fifth argument, would have silently compiled
with `this` implicitly converted to `bool true` — meaning every ordinary
Add or Edit would have secretly wiped the certificate's endorsements.
Caught during Claude Code's own review and fixed at both call sites before
reporting the step complete; confirmed correctly applied on independent
re-read.

**Three test-writing bugs, also self-caught and fixed during Claude Code's
own debugging cycle, not defects in production code:** a missing include; a
wrong test expectation that a certificate 20 days from expiry with one
endorsement would read Critical rather than the correct Survey Overdue
(unsatisfied later anniversaries outrank a merely-critical expiry, per the
existing step-6 rule); and `QMessageBox::done()` leaving `clickedButton()`
null, which made a simulated "Yes" read back as no button at all — fixed by
clicking the real button (`box->button(...)->click()`) instead. The final
test file, read in full during verification, reflects all three fixes
correctly, including a polling timer (rather than a zero-shot) to catch the
edit dialog once it exists.

Judgment calls made (Claude Code's, all reviewed and confirmed rather than
changed):
1. `selectedRow()` uses `m_table->selectionModel()->selectedRows()` rather
   than the spec's literal sample code (`QTableWidget::currentRow()`) —
   confirmed correct and a genuine improvement: `currentRow()` is Qt's focus
   cell, which can be set with nothing visibly selected, while the spec's
   own prose asked for "enabled only when the table has a selection." The
   deviation matches the spec's intent even though it doesn't match the
   exact code sample.
2. A failed `deleteAllForCertificate()` during renewal still lets the dialog
   accept — see above.

**New outstanding technical debt found during verification, not introduced
by this step but not previously flagged:** `CertificateListWidget.cpp` is
now 434 lines, well past the ~300-line guideline in `CLAUDE.md` §9. It grew
past that line sometime across steps 7–9 without being caught — step 7's
own note said it "grew but stayed under 300." Not split now: the file reads
as clearly separated methods (`reload()`, `fillRow()`, the renewal trio,
`addCertificate()`/`editCertificate()`), with no single obvious seam to
extract the way the statement splitter was pulled out of `MigrationRunner`
in step 3. Worth watching closely, since step 11 (Extensions) is likely to
touch this same file next.

Committed as `8ba1347` and pushed to `origin/main`, alongside this
`PROJECT-STATUS.md` update and `docs/certificate-renewal-spec.md`.

### Step 11 — Certificate short-term extensions ✅
Full design in `docs/certificate-extension-spec.md`, written after the
developer's own framing of the real-world event this step needed to
represent: sometimes class or flag issues a genuine short-term
certificate — a separate document — while a problem with the real
certificate's cycle is sorted out, and the original certificate is never
touched; once the problem is resolved, it simply continues being the
valid certificate it always was. That's a meaningfully different shape
from step 10's renewal, which edits the certificate's own row in place —
an extension changes nothing about the certificate itself. Built and
independently verified file-by-file against the spec — the migration, the
domain struct, both repository files, both dialog files, `CertificateState`,
both `CertificateListWidget` files, both `CertificateAlertProvider` files,
`CertificatesModule`, both `CMakeLists.txt` files, and the new test suite —
not just from Claude Code's own report. For the ~33 mechanical call-site
updates across four other test files, verification relied on the
compiler rather than re-deriving each one by hand: a missing
`CertificateExtensionRepository&` reference is a hard compile error, not
a silent behaviour change (unlike step 10's `bool isRenewal` footgun), and
the clean rebuild came back 193/193 build steps with zero errors. One
file's nine call sites were also spot-checked directly and all threaded
the new repository correctly. **No defects found.**

- `migrations/007_create_certificate_extension.sql` — the
  `certificate_extension` table. `expiry_date` is `NOT NULL`, unlike the
  certificate's own — a short-term certificate with no expiry isn't a
  meaningful concept, so there's no "does not expire" case to support
  here. No foreign key to `certificate`, the same trade-off as
  `endorsement.certificate_id`, resolved the same way for consistency. An
  index on `(certificate_id, is_deleted)` supports the one query shape
  this table is ever read by: "the active extension of one certificate."
- `modules/certificates/domain/CertificateExtension` — a plain struct,
  the same shape as `Endorsement`: `id`, `certificateId`, `expiryDate`,
  an optional `note`.
- `modules/certificates/data/CertificateExtensionRepository` — mirrors
  `EndorsementRepository`'s shape, including scope applied transitively
  through the parent certificate. `findActiveFor()` orders
  `created_at DESC LIMIT 1` even though `start()` already guarantees at
  most one active row — a defensive measure against a future sync merge,
  not a bug it's currently fixing. `start()` refuses a second active
  extension or a missing expiry date with a friendly message before any
  raw constraint. `resolve()` soft-deletes the active extension for a
  certificate (affecting zero rows is success, not failure) and — a
  detail the spec's prose under-specified but its edge-case list required
  — checks scope first, so resolving another vessel's certificate is
  refused with a message rather than silently affecting zero rows and
  reporting success.
- `modules/certificates/domain/CertificateState` gained a new optional
  parameter, `const std::optional<QDate>& activeExtensionExpiry`,
  defaulted to `std::nullopt` so every pre-existing call site is
  unaffected — confirmed by a dedicated test asserting omitting it is
  byte-identical to passing `std::nullopt` explicitly. When present, it
  takes over expiry-severity calculation through the exact same threshold
  logic already used for a certificate's own expiry, and survey
  evaluation is genuinely suppressed via the same "nothing to evaluate"
  path a certificate with no survey requirement already takes — no new
  `SurveySeverity` or `DisplayStatus` value was needed on either axis.
  The branch is checked **before** the `neverExpires()` guard, so an
  extension governs even in the (UI-prevented, but not repository-
  prevented) case of one somehow existing against a never-expiring
  certificate — confirmed correct: the alternative would silently discard
  a real document from an outside authority. `CertificateState` gained
  one new field, `activeExtensionExpiry`, a plain passthrough so the UI
  can ask this one question without a second lookup.
- `modules/certificates/ui/CertificateExtensionDialog` — a small dialog,
  its two fields (expiry date, optional note) built in directly rather
  than in a separate `*EditForm` widget, since nothing else needs to embed
  them. Defaults the expiry to three months out, with no date-range
  constraint — a plausible bridge-period starting point the user will
  usually overwrite with the date on the actual document.
- `modules/certificates/ui/CertificateListWidget` gained one button that
  does both directions of the action: "Start Short-Term Extension…" when
  nothing is active, "Short-Term Active — Resolve" when something is —
  resolving takes no confirmation dialog, since nothing is destroyed that
  couldn't simply be started again, and the row survives as history
  either way. Disabled with no selection or for a certificate that never
  expires, the same rule Renew already applies. **Renew is now also
  disabled while an extension is active** — renewing starts a fresh
  cycle, which doesn't fit while the real problem the extension bridges
  is still open; re-enabled the moment the extension is resolved. The
  Expiry Date cell shows the extension's date with a "— Short Term"
  suffix while one is active, with no new column, so every existing
  sort/filter behaviour stays untouched. Days Left now fills in for an
  extended certificate even when the certificate itself never expires,
  since the extension supplies a date to count to that the certificate
  alone does not.
- `modules/certificates/CertificateAlertProvider` — the second of exactly
  two places (`CertificateListWidget::reload()` is the other) that must
  look up a certificate's active extension and pass its date into
  `computeCertificateState()`, or the sidebar badge and daily banner
  would keep alerting on a paused survey schedule and disagree with what
  the list itself shows. Verified not just by reading the code but by
  Claude Code's own self-check: they temporarily reverted this one call
  site to prove the new test actually fails on exactly that gap, then
  restored it — the same discipline step 10's constructor-footgun catch
  showed, applied proactively this time rather than caught after the
  fact.
- `CertificatesModule` gained a `CertificateExtensionRepository` member,
  threaded into both `CertificateListWidget` and `CertificateAlertProvider`.
- 25 unit test suites total (was 24) — one new suite,
  `tst_CertificateExtension`, with 9 test functions covering spec §8's
  nine edge cases (starting an extension and seeing it on the list;
  refusing a second active one; the button disabled for a never-expiring
  certificate; resolving reverting the list to the certificate's own
  expiry; survey evaluation genuinely suppressed, not just hidden, proven
  against a certificate that would otherwise read Survey Overdue; Renew
  disabled while active and re-enabled once resolved; a resolved
  extension surviving as a soft-deleted row rather than vanishing;
  VESSEL-mode scope refusing another vessel's certificate for both start
  and resolve; and the alert provider genuinely excluding a
  paused-survey certificate, the self-checked case above) plus 4
  supporting functions (reading with nothing active or an empty id;
  refusing a missing expiry date or a nonexistent certificate; resolving
  with nothing active succeeding; and the "omitting the parameter changes
  nothing" guarantee). Call-site updates (not new test cases) ran to
  roughly 33 constructions across `tst_CertificateListStatus.cpp`,
  `tst_CertificateListWidget.cpp`, `tst_CertificateRenewal.cpp`, and one
  provider construction in `tst_Alerts.cpp`.

Judgment calls made (Claude Code's, all reviewed and confirmed rather
than changed):
1. The extension branch governing before the `neverExpires()` guard —
   confirmed correct, see above.
2. `resolve()` checking scope, which the spec's §5 prose didn't
   explicitly require but §8's edge-case list did — confirmed correct;
   the two sections of the spec genuinely pulled in different directions
   here, and resolving in favour of the edge case was the right call.
3. `findActiveFor()`'s defensive `ORDER BY created_at DESC` — confirmed
   reasonable, costs nothing today and protects against a future sync
   scenario the single-user case can't otherwise trigger.
4. No separate `*EditForm` widget for the extension dialog — confirmed
   correct; the spec only asked for the dialog files, and two fields
   don't justify a second class.
5. The three-month default with no date-range constraint — confirmed
   fine as a starting point, same convention as every other form's
   defaults in this app.

Committed as `73aed92` and pushed to `origin/main` (`7572a11..73aed92`),
alongside this `PROJECT-STATUS.md` update, `docs/certificate-extension-spec.md`,
and the rest of the 22-file change.

### `CertificateListActions` refactor ✅
Not a build-order step — a pure, no-behaviour-change split, planned at
the end of step 11 once `CertificateListWidget.cpp` crossed 566 lines.
`src/modules/certificates/ui/CertificateListActions.h/.cpp` now own the
two selection-driven buttons (Renew and the short-term extension
Start/Resolve) and everything that decides their enablement and labels;
`CertificateListWidget` keeps the table, `reload()`, and `fillRow()`.
A new `CertificateListRow.h` holds the three `Qt::UserRole` data-role
constants that used to be private to the widget's `.cpp` — they had to
become shared once one class writes them (`fillRow()`) and another reads
them back (every button-state check), with a `static_assert` in the
widget's `.cpp` tying the shared `kDataColumn` constant to the real
`Column::ListNumber` enum value, since the two are now declared in
different files.

Verified independently: `CertificateListRow.h`, both widget files, both
actions files, and the `CMakeLists.txt` registration all read exactly as
reported, object names are untouched (`renewCertificateButton`,
`certificateExtensionButton`, `certificateTable`, etc. — this is what
lets `MainWindow` and every existing test keep finding them by name),
and `git diff --stat -- tests/` being empty was independently confirmed
rather than taken on trust. **No defects found.** One small correction to
the write-up, not a problem: the two buttons are constructed with the
widget as their Qt parent, not with `CertificateListActions` itself — "it
owns its two buttons" is slightly loose, though harmless, since
`QBoxLayout::addWidget()` would reparent them to the widget regardless of
where they started.

`CertificateListWidget.cpp` came down from 566 to **396 lines** — still
over the ~300-line guideline, but the remaining bulk (`reload()` at
~105 lines, the constructor at ~90) doesn't have the same kind of clean,
single-purpose seam the selection-driven actions did. Accepted as shipped
rather than chased further for now; revisit only if either piece grows
into something with a real boundary of its own, the same "watch, don't
split without an obvious seam" call this file has gotten at every prior
size check.

Committed as `95ae54d` and pushed to `origin/main` (`ab2c0b6..95ae54d`).

### Step 12 — Certificate attachments ✅
Full design in `docs/certificate-attachment-spec.md`, reached only after
correcting two of my own first proposals: initially suggesting the
certificate's attachment slot be shared with an active extension's
document (rejected — the developer's own framing, mirroring step 11's
"an extension never touches the certificate's own data," applies here
too: the real document must stay untouched by an extension exactly like
the real dates do), and initially not accounting for the developer's
requirement to attach a whole folder of files, not just one. The
approved design has **two independent mechanisms**, not one: the
certificate's own attachment (one active row at a time in the new
`certificate_attachment` table, replace-and-archive, the same shape as
`certificate_extension`) and an extension's own document (a single
nullable `attachment_path` column directly on `certificate_extension` —
no archiving needed, since an extension row is already an immutable
historical record the moment it's resolved). Attaching works identically
for a single file or a whole folder; no file-type restriction
(explicitly declined by the developer). Built and independently verified
file-by-file against the spec — both migrations, the new
`core/ManagedFileStore`, the domain struct and naming function, both
repository/storage files in `data/`, the new `service/` layer (the
project's first real use of that folder), both new UI helper classes,
every changed dialog and widget file, `CertificatesModule`, both
`CMakeLists.txt` files, the shared test fixture, and both new test
files — not just from Claude Code's own report. One batch of the
mechanical call-site updates was also spot-checked directly (15 sites in
one file, all correctly threading the new service reference). **No
defects found.**

- `migrations/008_create_certificate_attachment.sql` — the
  `certificate_attachment` table: `id`, `certificate_id` (no FK, the same
  trade-off already made for `endorsement` and `certificate_extension`),
  `relative_path`, standard audit columns, an index on
  `(certificate_id, is_deleted)` supporting the one query shape this is
  ever read by.
- `migrations/009_add_certificate_extension_attachment_path.sql` — one
  nullable `ALTER TABLE` on the already-committed `certificate_extension`
  table, the same shape migration 004 used to add `list_number`.
- `core/ManagedFileStore` (new, in `core/` rather than the certificates
  module, deliberately — any future module needing file storage, the
  Document Management module on this project's own roadmap included,
  should use this directly rather than reinventing it). **The single most
  important correctness requirement in the whole step**:
  `ManagedFileStore::rootPath()` derives the managed data folder from
  `QFileInfo(Database::defaultFilePath()).absolutePath())`, never an
  independent `QStandardPaths` call — directly preventing a repeat of the
  step-7 incident recorded in `CLAUDE.md` §10.1, where two different
  resolutions of "the same" folder on the developer's machine were
  actually different files. Read in full and confirmed exactly correct.
  Claude Code's own addition beyond my spec: `setRootPathOverride()`, a
  process-wide override used by both the test fixture (a `QTemporaryDir`
  standing in for the managed folder) and `CLAUDE.md` §10.1's throwaway
  data directory — confirmed a sound, justified addition, not scope
  creep. Includes `copyInto()` (file-or-folder uniform, numeric suffix
  only on an actual collision, placed before the extension so the file
  stays openable), `moveToArchive()` (rename, not copy-then-delete, with
  a UTC-timestamp suffix), and `resolveAbsolute()` (an empty input
  returns empty, never the root folder itself — "no attachment" must
  never accidentally open the whole data folder).
- `modules/certificates/domain/AttachmentNaming::buildName()` — the pure
  naming function behind `<No.>_<Name>_<expiry date>`, e.g.
  `15D_Safety_Construction_2027-06-01`, with `_ShortTerm_` inserted before
  the date for an extension's own document. Assembled only from the parts
  actually present (a missing list number or a never-expiring
  certificate simply contributes nothing, rather than a stray
  underscore), sanitised against the Windows-legal filename character
  set (strictest of the three platforms, so one generated name is legal
  everywhere), with a dull "attachment" fallback for the edge case of no
  usable characters at all.
- `modules/certificates/data/CertificateAttachmentRepository` — SQL only,
  never touches the filesystem. `findActiveFor()` mirrors the extension
  repository's scope-filtering join and defensive `ORDER BY created_at
  DESC LIMIT 1`. `attach()` is one call doing both halves of a replace
  (soft-delete the active row, insert the new one) in a single
  transaction — simpler than the extension's separate `start()`/
  `resolve()`, because "replace" is the only operation this table ever
  needs. Refuses an absolute `relativePath` outright, rather than quietly
  storing one that would be wrong on the other side of a sync.
- `modules/certificates/data/CertificateAttachmentStorage` — the
  module's own file-layout convention
  (`attachments/<certificateId>/`, `.../archive/`, `.../extensions/`),
  written down in exactly one place on top of the generic
  `ManagedFileStore`, so the layout can't drift across call sites.
- `modules/certificates/service/CertificateAttachmentService` (new —
  not in my original spec, Claude Code's own addition, confirmed sound):
  the archive-then-copy-then-record sequence lives here because its
  *order* is the rule, and two screens (the list's Attachment menu and
  the renewal dialog) need to run it identically. Filesystem work happens
  before the database write, deliberately — a failure's bounded fallout
  is an orphaned file copy, never a database row pointing at nothing.
- `ui/AttachmentChooser` and `ui/AttachmentSourceField` (new, not in my
  original spec) — the file/folder picker and the reusable "choose a
  document" form field folded into both the renewal dialog and the
  extension dialog, so the two places offering this control behave
  identically rather than two hand-built copies drifting apart.
- `CertificateListActions` gained the Attachment button: one button with
  a three-entry menu (Open Current Attachment, shown only when something
  is actually attached rather than shown disabled; Attach File; Attach
  Folder) rather than three permanent buttons, since attaching is
  occasional and a row of mostly-disabled buttons reads as broken.
- `CertificateExtensionDialog` now takes the whole `Certificate` rather
  than its id and name, since the generated filename needs the
  certificate's number and name. Its document is copied in **before**
  `start()` is called — an accepted orphan-file risk on a failed
  `start()`, consistent with the "filesystem first" failure-direction
  rule above, since nothing has been written to the database yet for the
  dialog to roll back.
- `CertificateEditDialog` (renewal mode only) gained an optional
  attachment section, worded so that leaving it alone is obviously safe
  ("the document already attached stays as it is") — the renewal is the
  moment a new physical document actually arrives, so replacing it
  happens in the same motion, using the certificate's *new* expiry date
  for the generated filename.
- `CertificatesModule` now owns the attachment trio in dependency order
  (repository → storage → service) and hands only the service to the UI.
- 27 unit test suites total (was 25) — two new suites. `tst_ManagedFileStore`
  (12 real test functions) exercises the class with no database and no
  certificate involved at all, including the single most load-bearing
  assertion in the step (`rootPath()` really does come from
  `Database::defaultFilePath()`) and a same-relative-path-different-root
  test proving `CLAUDE.md` §6.9 in practice. `tst_CertificateAttachment`
  (16 real test functions) covers all nine of the spec's edge cases —
  first attach, replace-archives-the-old-one, folder attachment, file
  replaced by folder and back, resolving for both kinds, an extension's
  document being completely independent of the certificate's own
  (confirmed: starting or resolving an extension touches zero rows of
  `certificate_attachment`), VESSEL-scope refusal, the replaced row
  surviving soft-deleted, and the relative-path/different-root proof —
  plus the naming function's rules and the list button's enablement/menu
  states. Call-site updates ran across the certificate test suites that
  build a `CertificateListWidget`; one file's 15 sites were spot-checked
  directly and all correctly pass the new `CertificateAttachmentService&`.
  Clean rebuild confirmed: 0 errors, 0 warnings, 211/211 build steps;
  ctest 100% passed.

Judgment calls made (Claude Code's, all reviewed and a decision given
rather than silently accepted):
1. The two additions beyond my literal spec —
   `ManagedFileStore::setRootPathOverride()` and the whole
   `service/CertificateAttachmentService` layer — confirmed sound and
   kept exactly as built.
2. **Scope is checked only at the database-write step, after the file
   has already been copied to disk** (`CertificateAttachmentRepository::
   findActiveFor()` treats an out-of-scope certificate as "nothing
   found" rather than an error, giving the service no earlier signal).
   Claude Code proposed adding an `isInScope()` check as a new first step
   and left the decision to me. **Decision: accepted as-is, not fixed.**
   The real UI can never trigger this — every screen loads the
   certificate through `CertificateRepository::findById()` first, which
   already scope-filters before the attachment flow is ever offered.
   Adding a defensive check for a caller that doesn't exist yet is
   speculative generality for a module with exactly one caller today.
   Revisit if a second caller of the service ever appears — a future
   CSV import or the sync engine writing attachments directly, for
   example.
3. `CertificateListActions.cpp` grew to 319 lines, 19 over the
   ~300-line guideline, in the very step after it was extracted for
   exactly that reason. **Decision: left as shipped, not split again.**
   Forcing a second split for 19 lines right after the first one would
   be churn for its own sake; revisit only if this file is touched for
   an unrelated reason and the seam looks real at that point.

Committed as `04ab243` and pushed to `origin/main` (`6e4907d..04ab243`),
41 files, alongside this `PROJECT-STATUS.md` update, the `CLAUDE.md` §5
naming correction, and `docs/certificate-attachment-spec.md`. Later
amended in place to **`fdf2e22`** (tree hash unchanged, `git diff --stat`
against `04ab243` empty — only the commit message bytes changed) once
the BOM below was fixed, and force-pushed before anything else was built
on top of it.

**One real defect this round, caught after the fact rather than during
verification, and taking two tries to actually fix**: the commit subject
originally carried a leading UTF-8 BOM (`EF BB BF`), invisible in the
terminal but a real stray character in `git log`/GitHub/editors. I had
two wrong explanations in a row for it before the real one: back at
step 10 I said `git commit -F` strips a leading BOM from the message
file (wrong — confirmed by reading the actual bytes of `8ba1347`, which
still has one); then last turn I said piping the message into
`git commit -F -` was the reliable fix (also wrong — the developer's
first attempt at exactly that, `2074fc8`, still came out with the BOM,
and was caught and never pushed). The actual source is somewhere in how
PowerShell 5.1 writes a native command's stdin, not `Out-File` and not
the pipe — not fully root-caused, deliberately: the developer stopped
digging once a fix that provably works was in hand, which was the right
call. **What actually works**: write the message file explicitly with
`[System.IO.File]::WriteAllText($path, $msg, (New-Object
System.Text.UTF8Encoding $false))` — the BOM-less overload — verify the
file's own first bytes are `53 74 65 70` ("Step", no `EF BB BF`) before
handing it to git, then `git commit -F $msgFile` as usual. That
file-bytes check is now the thing to actually rely on; neither of my
two "should be fine" claims survived contact with the real bytes.

---

### Attachment addendum — per-vessel folders, Add/Edit attach control, Document & Issue Date columns ✅
Triggered by manual testing of step 12 itself: the developer raised four
real gaps after actually using it (where files are stored, no way to
attach from the ordinary Add/Edit dialog, no visible indicator of what's
attached, Issue Date missing from this screen). Design discussion
converged on `docs/certificate-attachment-addendum-spec.md`, with three
explicit judgment calls confirmed directly by the developer before any
code was written: a vessel's attachment folder is a **hard requirement**,
not an optional nicety, with **no silent fallback** if it's missing;
changing a vessel's folder **moves its existing documents** rather than
leaving them behind; and the attach control belongs in **both** Add and
Edit, not Edit/Renewal alone. Built and independently verified
file-by-file against the spec — all 40 files `git show --stat` lists for
the final commit, cross-checked line-by-line against what was actually
read, not just against Claude Code's own report. **No defects found.**

One process note worth recording honestly: partway through this
verification I read `CertificateListActions.cpp` without first
re-staging it for this pass, got the stale pre-addendum copy back, and
nearly reported a real-looking defect (a single-argument
`currentAbsolutePath()` call that would have been a compile error
against the new two-argument signature). Caught it myself before
reporting it, re-staged, and the actual current file was correct. Logged
here because it's the same class of mistake `CLAUDE.md` §10.1 already
exists to guard against in a different shape — trust the freshly-staged
bytes, not the last thing read.

- `migrations/010_add_vessel_attachment_root_path.sql` — one nullable
  `ALTER TABLE vessel ADD COLUMN attachment_root_path TEXT`. `NULL` means
  "not configured," which is what makes the hard-refusal rule in
  `VesselAttachmentRoots::rootForWriting()` possible with no special-case
  sentinel.
- `core/VesselAttachmentRoots` (new) — the policy class the addendum
  turns on. `rootForWriting()` checks
  `ManagedFileStore::rootPathOverride()` first (keeping every existing
  test's throwaway-directory pattern working unchanged), then the
  vessel's own stored path, returning empty with a friendly
  "set it in Settings" message if neither applies — never a fallback to
  the old installation-wide folder. `resolveForOpening()` is the one
  place that *does* fall back to the legacy location, deliberately, so
  an attachment made before this addendum still opens.
  `changeRoot()` validates (exists, writable, not already holding
  another vessel's `attachments/` folder), moves any existing subtree,
  and only then writes the database column — with a rollback (move the
  files back) if that final write fails, which is Claude Code's own
  addition beyond the literal spec and confirmed sound.
- `core/ManagedFileStore` — `rootPath()` now explicitly documented as
  the pre-addendum location (kept only so an old attachment can still be
  found), with `copyInto()`/`moveToArchive()`/`resolveAbsolute()` all
  taking the root as an explicit parameter instead of calling it
  internally. New `moveTree()`: rename first (same volume), recursive
  copy-then-delete-source as a cross-volume fallback, refuses a
  destination that already exists rather than merging into it, and
  **deliberately reports success if the copy succeeded but deleting the
  source failed** — reasoned that failing here would leave a full copy
  at both ends and make retry permanently impossible. Confirmed sound.
- `core/Vessel.h` / `VesselRepository` — `attachmentRootPath` is stored
  **absolute**, the one deliberate exception to `CLAUDE.md` §6.9's
  relative-path rule, with a new `setAttachmentRootPath()` as the only
  method that ever writes it. Deliberately **not** written by the
  ordinary `update()` — confirmed necessary, since `VesselEditForm`
  builds a fresh `Vessel` with this field always empty, so including it
  there would silently blank a vessel's folder on every routine edit.
- `service/CertificateAttachmentService` — gained `VesselAttachmentRoots&`
  as a third collaborator. Both `replaceCurrent()` and
  `addExtensionDocument()` check it as literally the first line of the
  function body, before any repository or storage call — confirmed this
  is what makes the refusal identical across all four attach entry
  points rather than something each screen has to remember to check.
- `ui/CertificateEditDialog` — the attachment section (from step 12,
  renewal-only) now appears in **all three modes**, worded per mode so
  doing nothing is obviously safe in Edit/Renewal ("stays as it is") and
  honest in Add ("no document is attached yet"). On Add, the attachment
  is applied *after* `create()` returns, using the id the repository
  just generated — there is nothing to name a file after until the row
  exists.
- `ui/CertificateListWidget` — two new columns: **Issue Date**, placed
  right after Name per the spec (a plain field, no computed state,
  sorted chronologically via the new `DateItem`); and **Document**, last,
  showing a plain glyph with no filename when something is attached and
  nothing at all otherwise. A single click anywhere in that column opens
  the document with no row selection needed first; every other column
  still ignores a single click, which is what keeps the behaviour
  unambiguous. Lives here rather than in `CertificateListActions`
  because it's driven by *which cell was clicked*, not by *what's
  selected* — the same distinction that already keeps Add Certificate
  out of that class.
- `ui/DateItem` (new) — the same "store the real value alongside the
  display text" pattern as `ListNumberItem`/`StatusItem`, so Issue Date
  (and Survey From/To, which silently gained the same fix) sort
  chronologically instead of as text.
- `app/AttachmentFolderSettings` (new) + `app/SettingsPage` — one row per
  vessel on the Settings screen (vessel name, current folder, a
  "Choose…" button), built from `VesselRepository::list()` so VESSEL vs
  OFFICE scoping is inherited rather than re-checked. Deliberately
  outside the existing Save button's reach: choosing a folder physically
  moves files the moment it's chosen, which can't share one Save with a
  numeric threshold that only takes effect when clicked.
- `CLAUDE.md` §6.9 — wording corrected, with Claude Code's own proposed
  text approved as-is: paths are relative to the vessel's configured
  folder, `attachment_root_path` is named as the one absolute exception
  and why, with a pointer to the addendum spec and a note that there was
  one folder per installation before it.
- 3 new unit test suites (30 total, was 27): `tst_VesselAttachmentRoots`
  (10 functions — the refusal and its message, the override still
  winning over a configured vessel per §2.4, all of §2.5's
  move/refuse/no-op cases, and a pre-addendum attachment still resolving
  after a folder is configured), `tst_CertificateAttachmentFolders` (11
  functions — the same refusal reached through the service from a bare
  vessel id, the Add/Edit/Renewal attachment section, the Document
  column's mark-only-what's-attached and click-does-nothing-on-empty
  behaviour, and the Issue Date column's chronological sort end to end),
  `tst_AttachmentFolderSettings` (5 functions — one row per vessel in
  OFFICE mode, exactly one in VESSEL mode, the unconfigured/configured
  label text). `tst_ManagedFileStore` gained 4 functions for
  `rootPathOverride()`/`moveTree()`. Test-function count: **307 → 337
  (+30)**, counted directly from the suites (ctest reports 27 → 30
  *suites*, not functions) and cross-checked against the diff.
  **Clean rebuild confirmed from an actually-deleted `build` directory**
  (not an incremental build, unlike this addendum's first pass): 232/232
  build targets, 0 errors, 0 warnings; ctest 100% passed, 30/30 suites.
  Both the full `git show --stat` and the full raw `ctest` output were
  pasted back and read directly rather than taken on trust.

Three script corrections caught by Claude Code, not by me, worth
recording since I handed over a script that would have failed outright:
the generator must be `Ninja`, not Visual Studio/MSVC — this Qt 6.11.1
install is MinGW-built, and MSVC cannot link against it (`CLAUDE.md` §2
already pins MinGW/GCC on Windows; I'd simply forgotten it when writing
the rebuild script); `UTF8Encoding` lives in `System.Text`, not
`System.IO` as I'd written it; and `Format-Hex` doesn't exist in
PowerShell 5.1, so the byte check ran through `od` in Bash instead
(itself not a PowerShell command, so that moved to Bash on the second
try too).

Committed as **`93e23c0`** (code, 40 files, 2572 insertions / 216
deletions) and, separately, **`28b45d3`** (the two docs files that were
prepared before `93e23c0` but not included in it: the addendum spec
itself — which `93e23c0`'s own code comments reference by path — and the
manual-test-checklist.md path correction from before this addendum
began). Both confirmed genuine directly from `.git/logs/HEAD` and
`CLAUDE.md`'s actual on-disk content, not from the report alone. Pushed
to `origin/main`, together with the verification write-up commit
`9fac67e` below.

### Attachment addendum 2 — flattened storage, Delete Certificate/Attachment, endorsement edit/delete, nearest-due-date naming ✅
Triggered the same way the first addendum was: the developer manually
tested the real build and reported one bug (an archived file lost its
extension — `.pdf__archived-20261102T...` rather than
`...__archived-20261102T....pdf`, unopenable from a file browser) and
three feature gaps (the `attachments/<id>/` folder structure created two
unwanted subfolders the developer never asked for; there was no way to
delete a certificate or an attachment at all; endorsements were still
add-only from step 6, with no way to correct or remove one). Design
discussion — including one real misunderstanding on my side, caught and
corrected before anything was built: I initially read "no archive
folder should be" as "stop archiving entirely," a reversal of
`CLAUDE.md`'s "nothing is ever actually deleted" principle for files; the
developer actually meant "no archive *sub*folder *per certificate*,"
i.e. keep archiving but flatten it to one shared folder — converged on
`docs/certificate-attachment-addendum-2-spec.md`. Built, and
independently verified file-by-file against the spec: **all 44 code
files** in the final commit (33 modified, 11 new) read in full,
line-by-line, against both the spec and Claude Code's own 16 stated
judgment calls. **No defects found.**

This addendum deliberately reverses part of step 12's own "nothing is
ever actually deleted" rule for files, on the developer's explicit
instruction: a **replace** still archives the superseded file exactly as
before, but a **delete** (new for this addendum, for either a whole
certificate or a single attachment) now genuinely removes the real file
from disk, with no archive copy — because a delete exists specifically
to correct a mistake, not to record a supersession worth a trail.

- `core/FileTree` (new) — pulled out of `ManagedFileStore.cpp` along a
  real seam (Claude Code's own split, confirmed sound): the pure
  filename/path mechanics — `nameWithMarker()` (the bug fix: inserts the
  disambiguating marker *before* the extension via
  `QFileInfo::completeBaseName()`/`suffix()`, not after it),
  `removeAnything()`, `copyRecursively()`, `isSameOrInside()` — with
  nothing in it that touches a database or a certificate.
- `core/ManagedFileStore` — new `deleteAt()` (a genuine delete, guarded
  by `isContainableRelativePath()` against an absolute path, `.`, `..`,
  or anything starting `../` — the backstop behind `CLAUDE.md` §10.1,
  confirmed it cannot be made to resolve to the vessel's whole folder),
  new `entriesIn()` and `moveNamedEntries()` (checks every destination
  name before moving anything, so a vessel's documents can never end up
  split across two folders on a mid-move collision; rolls back
  everything already moved on any failure).
- `core/VesselAttachmentRoots` — new `kMarkerFileName =
  ".ship-management-vessel"`, a hidden file holding just a vessel's id,
  replacing the old "does this folder already contain an `attachments/`
  subfolder" ownership check now that flattening removed the structure
  that check relied on. `changeRoot()` carries the marker with the move
  but never with the documents themselves (confirmed by a passing test
  that was initially a failing one during Claude Code's own development
  — a real gap a test caught, the same class of lesson as the addendum-1
  stale-read note above).
- `modules/certificates/data/CertificateAttachmentStorage` — new shared
  `kArchiveSubfolder = "archive"`, one per vessel rather than one per
  certificate; `replaceCurrent()`/`addExtensionDocument()` now pass an
  empty "the vessel folder itself" sentinel as the destination, which is
  what the flattened layout means in code. New `deleteStored()`.
- `modules/certificates/data/CertificateAttachmentRepository` — new
  `detach()`: soft-deletes the active row with no replacement inserted,
  under the same vessel-scope check `attach()` already uses.
- `modules/certificates/data/CertificateRepository` — new
  `softDeleteWithChildren()`: one transaction, soft-deleting
  endorsements, the active extension, and the attachment row by
  `certificate_id` before the certificate row itself, with a
  zero-rows-affected race check. The *files* are deliberately not
  touched here — that is `CertificateDeletionService`'s job, run
  afterwards, so a failed database write can never find a document
  already destroyed.
- `modules/certificates/service/CertificateDeletionService` (new) — the
  small service that knows the files have to go *after* the rows (the
  opposite order from attaching, and the opposite reasoning, stated the
  same way: a failed delete of the row must not find the file already
  gone). Explicitly, permanently does **not** try to sweep the shared
  `archive/` folder for a deleted certificate's older copies — once a
  file is renamed into that shared folder there is no reliable way back
  from it to the certificate it came from, and guessing from the
  filename would eventually delete somebody else's document. Covered by
  its own test, `archivedCopiesAreLeftAloneWhichIsTheKnownLimitation()`,
  specifically so the gap reads as a decision, not an oversight.
- `modules/certificates/service/CertificateAttachmentService` —
  `replaceCurrent()` lost its `expiryDateForName` parameter and gained a
  private `nameDateFor()` instead, which reads `EndorsementRepository`,
  `CertificateExtensionRepository` and `AppSettingRepository` (three new
  collaborators) to work out which date is soonest. New `deleteCurrent()`
  and `deleteStoredFile()`.
- `modules/certificates/domain/CertificateState` — new `dueDate` field,
  set at exactly the same three call sites `daysLeft` already is (the
  active-extension branch, the outstanding-survey branch, the
  nothing-outstanding branch) — confirmed directly, not just read as a
  comment's claim — specifically so the two can never disagree. This is
  the field the attachment filename now reads: whichever of the
  certificate's expiry, its next annual window, and its next
  intermediate window is soonest, which is the same question the list's
  Days Left column already answers.
- `ui/CertificateAttachmentActions` (new, split out of
  `CertificateListActions.cpp`) and `ui/EndorsementSection` (new, split
  out of `CertificateEditDialog.cpp`) — both real seams, confirmed sound:
  the first is everything about *the document* on the selected row
  (open/attach/delete), the second is everything about *the
  endorsements* recorded against the certificate being edited (now with
  Edit and Delete buttons alongside the existing Add). Object names
  carried over unchanged from before the split, which is how the
  existing tests kept finding them with no changes of their own.
- `ui/CertificateListActions` — new `deleteButton()` and
  `deleteCertificate()`, last in the button row and furthest from Add
  Certificate. Unlike Renew and the extension button, it applies **no**
  never-expires rule and **no** active-extension rule: any certificate
  can have been entered by mistake.
- Endorsement edit/delete — `EndorsementRepository` gained `update()`
  (an ordinary UPDATE onto the same row, never a new one) and
  `softDelete()`, both addressed by the endorsement's own id via a new
  `loadParentOfEndorsement()` helper rather than trusting a
  `certificateId` on the struct, specifically so a correction can't
  silently re-parent an endorsement onto a different certificate.
  `EndorsementEditForm::setEndorsement()` offers a stored survey type
  the certificate no longer requires rather than dropping it — including
  correcting a fixed single-type label that could otherwise still say
  "Annual" while editing an Intermediate row, a late fix from Claude
  Code's own review pass, confirmed correct against its own test.
- 3 new unit test suites (33 total, was 30), counted directly from each
  file's `private slots:` block rather than taken from the report:
  `tst_CertificateAttachmentNaming` (7 test functions, plus `init`/
  `cleanup` — the nearest-due-date rule against the pure domain function
  directly, both ways the annual/intermediate race can go, the
  active-extension case, and the "computed once, never renamed" rule),
  `tst_CertificateDeletion` (12 test functions — covering both delete
  actions against every case in the spec's §9, including the accepted
  archive limitation and the vessel-scope refusal), `tst_EndorsementEditDelete`
  (12 test functions — the repository, the form, and the section,
  including the stored-type-no-longer-required case for both the combo
  and the single-type label). The remaining growth behind the overall
  **337 → 380 (+43)** test-function count comes from new functions added
  to the 5 modified suites (`tst_ManagedFileStore`,
  `tst_VesselAttachmentRoots`, `tst_CertificateAttachment`,
  `tst_CertificateAttachmentFolders`; `CertificateTestSupport.h` is
  shared fixture code, not a suite of its own).
  **Clean rebuild confirmed from an actually-deleted `build` directory**:
  254/254 Ninja build steps, 0 errors, 0 warnings; ctest 100% passed,
  33/33 suites. The one wrinkle, caught and explained by Claude Code
  rather than by me: the rebuild script I handed over omitted
  `-DCMAKE_BUILD_TYPE=Debug`, so this particular clean build was
  unconfigured (closer to Release — `QT_NO_DEBUG` defined, no `-g`) rather
  than Debug like the previous two rounds. Accepted as still valid:
  `QVERIFY`/`QCOMPARE` are ordinary runtime checks unaffected by
  `QT_NO_DEBUG`, the codebase has zero `Q_ASSERT` calls anywhere, and the
  project's one `static_assert` is compile-time regardless. Not redone;
  the build-type flag goes back into the rebuild script template for
  next time. Both the full Ninja build log and the full raw `ctest`
  output were pasted back and read directly, as was `git status` before
  and after the commit.

Committed as **`e0c4db7`** (45 files — 33 modified, 11 new code files,
plus the spec doc — 4501 insertions / 640 deletions). Pushed to
`origin/main`, together with the verification write-up commit `1463028`
below, and in turn with both rounds that followed it.

### Survey deadline fix — Days Left and attachment naming always count to window close ✅

Triggered by the developer manually testing the refreshed addendum-2
build: a certificate whose annual survey window hadn't opened yet had
its attached file named after the date the window *opens* — a year or
more before any real deadline. Diagnosed as original step 6/11 logic
(`CertificateState::dueDate`/`daysLeft` switched between the window's
opening and its closing date depending on whether today was before or
after the opening), not an addendum-2-only bug — addendum 2's
`nameDateFor()` just started reading the same field, which is what
surfaced it. Confirmed with the developer before writing anything: the
window's **close** is the real deadline (the last day the survey can
still be done); the opening is a heads-up, which is what should still
drive `SurveySeverity`/the colour, not the number.

Spec: `docs/certificate-survey-deadline-fix.md`.

- The entire production change is one line, in `CertificateState.cpp`'s
  outstanding-survey branch: `dueDate` is now unconditionally
  `reported.window.closes`, dropping the old before/after-opening
  ternary. The two branches around it (active extension, nothing
  outstanding) were already correct and untouched.
- `CertificateState.h`'s `dueDate`/`daysLeft` comments rewritten to state
  the single rule — a fix Claude Code made on its own initiative, beyond
  what the spec required, and confirmed correct against its own
  description.
- Three test functions changed, not the two the spec named — Claude
  Code's full read of `tst_CertificateAttachmentNaming.cpp` (as the spec
  specifically asked for, rather than a blind find-and-replace) turned up
  a third: `anAlreadyAttachedFileIsNotRenamedWhenTheScheduleMoves()`,
  which really was exercising the pre-opening case and needed the same
  change. `theIntermediateWindowWinsWhenItIsTheOneDue()` was checked and
  confirmed to need no change, since today is already inside that window
  in that test either way.
- `tst_CertificateStateSurvey.cpp`: one function changed
  (`daysLeftAlwaysCountsToTheWindowClose()`), with an explicit `!= 10`
  regression guard proving the fix actually changes behaviour rather
  than merely restating a weaker assertion.
- `tst_CertificateListStatus.cpp`: one function changed
  (`upcomingSurveyWindowIsShownLongBeforeItOpens()`);
  `tst_CertificateListWidget.cpp` needed no change (its only Days Left
  reference is the column header text) — both confirmed by direct search
  rather than assumption, per the spec.
- `certificate-control-spec.md` §4.4 rewritten to the single
  always-counts-to-close rule; `certificate-attachment-addendum-2-spec.md`
  §7 given a correction note rather than a rewrite, preserving its
  "single source of truth" bullet.
- **Verified independently, file by file**: all 7 changed files (2
  production, 4 test, 2 docs — `tst_CertificateListWidget.cpp` needed no
  change) read in full. Zero defects found.
- Full clean rebuild + `ctest` run as part of Claude Code's own report;
  no separate ritual re-requested, since the change was small, isolated,
  and already proven both ways — before/after, with the exact numbers
  the spec predicted (273 → ~455 days on the certificate that surfaced
  the bug).

Committed as **`95739fd`** (7 files) and pushed.

### InstallationContext storage fix — held by value, not by reference ✅

A separate, real bug surfaced as a side effect of the deadline-fix
round: every class taking an `InstallationContext` in its constructor
stored it as a `const InstallationContext&` member, which dangles the
instant a caller passes a temporary — the normal way to construct one
(`CertificateRepository repo(db, officeContext());`). Survives an
unoptimised build because the stack slot usually still holds the old
bytes; breaks under `-O2`, where the slot is reused. This had already
caused a real Release-build crash in `tst_CertificateAttachmentNaming`
and silently-wrong VESSEL-mode scope filtering in two other suites,
found and only partially fixed in an earlier, untracked round (the
shared `AttachmentFixture` properly fixed; `tst_AttachmentFolderSettings.cpp`
and `tst_EndorsementEditDelete.cpp` each had only one call site of
several patched).

Two options were weighed: naming a local at each of the ~50 affected
test call sites (mechanical, test-only), or changing every affected
class to hold the context by value instead (a small production change).
Chose the latter — `InstallationContext` is immutable with no setters,
so copying it is free of consequence, and it closes the whole backlog at
the type boundary rather than relying on every future caller
remembering the lifetime rule, which this very round's history showed
doesn't reliably hold.

Spec: `docs/installation-context-storage-fix.md`.

- The grep the spec asked for (don't trust the "7 repositories" estimate)
  turned up **8** classes: the five repositories named in the spec
  (`VesselRepository`, `CertificateRepository`, `EndorsementRepository`,
  `CertificateExtensionRepository`, `CertificateAttachmentRepository`)
  plus three non-repository classes with the identical bug shape —
  `VesselDetailPage`, `CertificateAlertProvider`, `CertificatesModule`.
  None of the three had ever actually hit the bug in production (each is
  constructed once, from something that outlives it), but the hazard was
  identical.
- Each class's constructor signature is unchanged (`const
  InstallationContext&` parameter, as `CLAUDE.md` §9 asks for passing);
  only the member's own type changed, from reference to value. Every
  header needed its forward declaration of `InstallationContext`
  replaced with a real `#include`, since a value member needs the
  complete type.
- The explanatory comment lives once, on `InstallationContext` itself,
  with a one-line pointer at each of the 8 members — chosen over 8
  copies of the same five-line rationale.
- The two partially-fixed test files from the earlier round were
  reverted to their original, pre-that-round form rather than finished
  off call-site by call-site: this makes the fix in each repository the
  thing being tested (the exact code that crashed in Release now
  passes), and avoids leaving 1-of-5 and 1-of-11 call sites looking
  specially correct for no remaining reason.
- A fourth stale comment, outside the spec's own list — in
  `tst_CertificateAttachmentNaming.cpp`, committed during the
  deadline-fix round — asserted the now-false claim that repositories
  store the context by reference. Found and corrected by Claude Code's
  own sweep, not the spec.
- One process note: a first attempt to edit the three near-identical
  repository headers with a scripted `perl -0pi` pass double-encoded
  their existing UTF-8 `§` characters (`§5` → `Â§5`). Caught in
  verification, the three files reverted and redone as ordinary edits; a
  follow-up grep for `Â`/`â€` across `src/` and `tests/` came back clean.
- **Verified independently**: all 13 changed files (9 production, 4
  test) read in full against the report's claims. Confirmed, via the
  files' own timestamps on disk rather than the report, that exactly 13
  files were touched — correcting the report's own first count of "11."
- Full clean rebuild in both Debug and Release from an actually-deleted
  `build`/`build-release`, `ctest` 33/33 in both. The three
  originally-affected suites re-run individually in Release and
  confirmed passing while constructing straight from a temporary — direct
  proof of the repository fix, not a workaround: `tst_CertificateAttachmentNaming`
  (previously `SEGFAULT 0xC0000005`), `tst_AttachmentFolderSettings`
  (previously returned 2 vessels, expected 1), `tst_EndorsementEditDelete`
  (previously read another vessel's row).

Committed as **`ad76f4c`** (13 files: 11 code + the two spec docs for
this fix and the deadline fix, both of which had been referenced by
name in earlier commits — `95739fd` included — without ever being added
themselves; 389 insertions / 40 deletions) and pushed (`95739fd..ad76f4c`).

### Step 13 — CSV import/export & PDF export ✅

Per `CLAUDE.md` §11's build order. Design discussion held first, over
several rounds: CSV before `.xlsx` (reconfirmed), import/export scoped
to one selected vessel rather than the whole fleet, available in **both**
installation modes rather than office-only, and PDF export added to the
step's scope at the developer's request — a simple current-state
statement for handing to a third party (a surveyor, a port state
inspector), not the fuller reporting step `CLAUDE.md` §11 still has
planned separately as step 14.

Spec: `docs/certificate-import-export-spec.md`. Went through two real
revisions before implementation, both worth recording because of what
they changed:

- **v1.0 → v1.1, a real design defect, caught by Claude Code's own
  review of the spec before writing any code.** v1.0 specified
  `Last Annual Survey`/`Last Intermediate Survey` as single-date
  columns. `SurveySchedule::evaluateAnnual()` matches each anniversary
  to the *earliest* still-unclaimed endorsement of the matching type, in
  date order — so a single "most recent" date for a certificate that is
  mid-cycle (several annuals already done, more still to come) would be
  claimed by the *first* anniversary, leaving every earlier one to come
  back falsely `Overdue`, with a fabricated "completed late" note
  invented from a date that was never actually that anniversary's own
  survey. Verified directly against `SurveySchedule.cpp` before
  accepting the correction. Fixed in v1.1 by replacing both columns with
  semicolon-separated date lists — `Annual Surveys`, `Intermediate
  Surveys` — so an import feeds the real survey history into the exact
  matching logic live data entry already uses, with no special case.
  `tst_CertificateCsvImportSurveys.cpp`'s
  `asingleMostRecentDateWouldHaveReportedTheSameShipOverdue()` proves
  the regression directly, as a counterfactual against the same
  certificate the positive test uses.
  - The same review surfaced roughly a dozen smaller corrections, all
    independently verified against the actual source before being
    accepted: `findById()` is unscoped in OFFICE mode, so row-matching
    had to go through `list(vesselId)` instead, never `findById()`; no
    callable validator existed before `create()`/`update()` (`validate()`
    was private, and the dialog never called a separate one), which the
    preview screen needs; the CSV dialect, header-matching rules, and a
    missing cross-field rule (blank expiry paired with a survey flag)
    were all unspecified; `previous_certificate_id` doesn't exist as a
    column at all, not merely "out of scope"; `list()` sorts by name, not
    by list number, so export has to sort explicitly; and the PDF needs
    more collaborators than first named (see below).
- **My own spec undercounted the PDF's collaborators.** v1.1 said four
  repositories (`CertificateRepository`, `EndorsementRepository`,
  `CertificateExtensionRepository`, `AppSettingRepository`).
  `CertificateReportBuilder` correctly takes a fifth, `VesselRepository`,
  for the vessel name and IMO in the heading — content the spec itself
  required but didn't connect to the collaborator count. Caught in my
  own verification of the implementation, not by Claude Code; the code
  was right, the spec's own list was inconsistent with itself.

Implementation, verified independently and file by file against the
actual source (not taken from the report):

- `validateCertificate()`/`validateEndorsement()` extracted from the two
  repositories' private `validate()` methods into
  `modules/certificates/domain/CertificateValidation.h/.cpp`, moved
  verbatim, each repository's own `validate()` now one line delegating
  to the domain function. This is what lets the import preview ask
  "would this be accepted" without attempting a write, and it's the only
  change to existing production *rules* in this step — confirmed no
  rule's behaviour changed, only where it lives.
  - Claude Code additionally extracted `validateEndorsement()`, not just
    `validateCertificate()` as the spec's own wording emphasised, because
    §6's "preview validates a survey date exactly as the Add Endorsement
    dialog would" needs it too — caught by Claude Code's own test, which
    found a survey dated before its certificate's issue date previewing
    as importable before the fix. Correct call; the spec's emphasis was
    just on the certificate side, not a scope boundary.
- `CertificateRepository::listDeletedIds()` — a new, scope-filtered
  repository method (identical filter to `list()`), added because
  neither `list()` nor `findById()` can answer "was this id here and
  deleted," which the "New (previously deleted)" preview outcome needs.
  A genuinely new repository method the spec didn't anticipate by name,
  but it doesn't contradict anything the spec promised about
  `EndorsementRepository` specifically.
- `core/CsvFile.h/.cpp` — a hand-rolled RFC 4180 reader/writer (Qt ships
  none), CRLF + UTF-8 BOM on write, CRLF-or-LF and optional BOM on read.
  Placed in `core/`, not the certificates module, on the same test
  `CLAUDE.md` §5 already uses elsewhere: a CSV file is not a certificate
  idea, and the modules still to come (spares, purchasing) will want
  tabular I/O of their own.
- `core/TransactionScope.h` — a small header-only RAII transaction
  (commit-or-rollback-on-scope-exit), for the one case in this module
  where two repositories have to succeed or fail together and neither
  owns both tables. Also core, for the same reason as `CsvFile`.
- `modules/certificates/service/`: `CertificateCsvValues` (cell-level
  codecs: dates, booleans, the semicolon date-list syntax),
  `CertificateCsvFormat` (the column list, header mapping, row
  parsing), `CertificateCsvFileChecks` (the two whole-file refusals —
  Vessel IMO mismatch, duplicate Id — run before any row is parsed),
  `CertificateCsvExporter`, `CertificateCsvImporter` (holds a
  `QSqlDatabase&` solely for the `TransactionScope` around its write
  loop; issues no SQL of its own, every write goes through
  `CertificateRepository`/`EndorsementRepository`), and
  `CertificateReportBuilder` (the PDF statement, `QTextDocument` +
  `QPrinter`, per `CLAUDE.md` §2 — landscape A4, generation date/time in
  the heading).
- `modules/certificates/ui/`: `CertificateIoActions` (the three
  buttons — Import CSV, Export CSV, Export PDF — gated only on "a
  vessel is selected," in both installation modes, split out of
  `CertificateListWidget` along the same seam `CertificateListActions`
  already uses) and `CertificateImportPreviewDialog` (the mandatory
  dry-run preview; blocked entirely while any row is Invalid; per-row
  overwrite tick defaulting to unchecked/skip).
- Row-matching confirmed to go through `list(vesselId)` read once into
  memory, never `findById()` — closes the cross-vessel corruption risk
  the spec review flagged, and reads directly on why the "no foreign
  key on `certificate.vessel_id`" trade-off (see "Decisions confirmed
  by the developer") stays low-risk: import never bypasses the
  repository's own scope checks to reach the table directly.
- `CertificateState.h/.cpp` gained `displayStatusLabel()`, the status
  text pulled out of `StatusItem` (which now just delegates to it) so
  the PDF statement — built in the service layer, which can't include a
  `ui/` header — shows the same words as the screen from one shared
  source rather than a second copy.
- Two new files split to stay under `CLAUDE.md` §9's ~300-line
  guideline before they were ever committed: `CertificateCsvFormat.cpp`
  (would have been 355; the cell codecs became `CertificateCsvValues`,
  now 286/75) and `CertificateCsvImporter.cpp` (would have been 382; the
  whole-file refusals became `CertificateCsvFileChecks`, now 297/74).
  Both confirmed at those exact line counts directly, not taken from
  the report.
- **A reporting discrepancy, caught in verification, not a code
  defect.** The implementation report claimed "seven suites registered,
  six with migrations." Counted the actual `ship_add_migrations()` calls
  in `tests/CMakeLists.txt` directly: it's five, not six —
  `tst_CsvFile` and `tst_CertificateCsvFormat` correctly have none,
  since both test pure functions with no database. The code is right;
  the sentence describing it undercounted by one, the same shape of
  small miscount as the "11 vs. 13 files" one two rounds ago.
- **Verified independently**: every new and changed production file
  read in full against the actual source on disk — the importer, the
  format/value/file-check trio, the exporter, the report builder, both
  new UI classes, both repository `validate()` delegations, the new
  `listDeletedIds()` method, `CertificateState`/`StatusItem`'s label
  move, `CertificatesModule`'s wiring, `CertificateListWidget`'s new
  button row and constructor, and both `CMakeLists.txt` files. The
  central design fix was verified two ways: by reading
  `CertificateCsvImporter.cpp`/`CertificateCsvFormat.cpp` directly, and
  by reading `tst_CertificateCsvImportSurveys.cpp`'s mid-cycle test and
  its counterfactual, which exercises the exact regression the fix
  exists to prevent.
- Full clean Debug + Release rebuild and `ctest`, both reported
  40/40 passing (118 new test functions across 7 new suites, 33
  pre-existing suites unchanged) — taken from Claude Code's own report,
  not independently re-run, since the file-by-file read above already
  covered the part of the report most likely to be wrong (the design
  itself) rather than the part a test run would catch (whether it
  compiles and the assertions hold).
- One small, optional polish item, not acted on: `TransactionScope::commit()`
  marks the transaction closed (`m_open = false`) before calling
  `QSqlDatabase::commit()`, so if the underlying commit itself fails,
  the destructor never attempts a rollback, leaving the transaction's
  real state with the driver ambiguous. Rare in practice — `commit()`
  failing at all against a local SQLite file is already an edge case the
  importer surfaces as an error — but worth a one-line fix (check the
  commit's own result before clearing `m_open`) if this class gains a
  second caller.

Not committed yet at the time of writing — this entry was prepared
alongside the verification above so it can go into the same commit as
the code, per the project's usual practice of committing the status
update together with the step it describes.

---

### Certificate list usability fixes — resizable columns, multi-select bulk delete, placeholder issue date on import ✅

Three fixes raised by the developer after real use of the second
(work-laptop) install: dragging the Name column did nothing useful, since
the old Stretch mode split the header evenly and clipped long certificate
names with no way to see the rest; cleaning up a duplicate import (caused
by re-importing an edited copy of the *original* hand-built file rather
than a freshly exported one — expected behaviour of `create()` always
minting a fresh id, not a bug) meant deleting rows one at a time; and a
CSV row with no real issue date blocked the whole file rather than being
handleable. The third started as an ad hoc fix — three rows in the
developer's own import file were hand-patched with a flagged `1990-01-01`
placeholder rather than excluded — and was generalized into an opt-in
checkbox during this round's design pass.

Spec: `docs/certificate-list-usability-fixes.md`.

Implementation, verified independently, file by file, against the actual
source, not taken from the report:

- **§1 Column widths.** `CertificateListWidget`'s header resize mode
  changed from Stretch to Interactive, with `setStretchLastSection(true)`
  so widening the window doesn't leave dead space after the last column.
  Starting widths changed too — Name now gets 320px, previously an equal
  share with every other column. New `TableHeaderSettings.h/.cpp`
  (62/79 lines) wraps `QHeaderView::saveState()/restoreState()` behind a
  `QSettings` key (`ui/certificateList/headerState`), connecting
  `sectionResized`, `sectionMoved`, and `sortIndicatorChanged` all to
  `save()`. Sequencing confirmed directly: `restore()` runs, then the
  hardcoded `list_number`-ascending default is applied, then
  `startSaving()` begins — in that order, so a returning user's own
  layout wins over the fresh-install default, and restoring never fires
  the header's own signals back into a half-applied save.
  - A real consequence, surfaced by Claude Code's own review rather than
    asked for by the spec: `saveState()` carries the sort column in the
    same blob as the widths, so a user's last manual sort now persists
    across restarts too, quietly superseding step 7's "`list_number`
    ascending, always" default. Raised as an open question; the
    developer's call was to keep it — sort behaves exactly like column
    widths, "however I left it is what I get back" — rather than force
    the hardcoded default every launch. `docs/certificate-crud-spec.md`
    §8.2 is now corrected to describe this (see "Decisions confirmed"
    below).
- **§2 Multi-select + bulk delete.** `CertificateListWidget`'s selection
  mode changed from `SingleSelection` to `ExtendedSelection`. New
  `CertificateSelection.h/.cpp` (52/85 lines): five free functions, with
  the one rule that matters sitting in exactly one place —
  `singleRow()` returns -1 for zero selected *or* more than one
  selected, treating "ambiguous" exactly like "none." That rule alone is
  what disables Renew, the extension button, and (see below) Attachment
  the moment a second row is picked, with no separate check at any call
  site. New `CertificateBulkDeletion.h/.cpp` (56/39 lines): a
  best-effort loop over `CertificateDeletionService::deleteCertificate()`,
  deliberately with **no outer transaction** — `softDeleteWithChildren()`
  already transacts per certificate and SQLite has no nested
  transactions, so one certificate failing doesn't block the rest. New
  `CertificateDeleteActions.h/.cpp` (90/245 lines): resolves the
  selected ids, falls back to the existing single-certificate
  confirmation wording (byte-identical) when exactly one certificate is
  selected, otherwise a capped bullet list (10 names) with the combined
  survey count across all of them, then a capped summary (10 lines)
  only when something actually failed or left an attachment file
  behind.
  - **A real gap in my own spec**, caught by Claude Code's own review
    before implementing, not by me. The spec said Renew, the extension
    button, and Attachment "already go through these helpers," implying
    Attachment needed no change. It didn't: `CertificateAttachmentActions`
    kept its own private, unshared "first selected row" lookup, which
    would have stayed enabled through a multi-select and silently acted
    on an ambiguous row. Fixed by pointing it at the same
    `CertificateSelection::singleCertificateId()`, with its own
    regression test. Verified directly:
    `CertificateAttachmentActions::selectedCertificateId()` now calls
    `CertificateSelection::singleCertificateId(m_table)` rather than any
    private lookup of its own.
- **§3 Placeholder issue date on import.** New
  `CertificateIssueDatePlaceholder.h/.cpp` (60/61 lines): `date()`
  returns `1990-01-01`; `noteFor()` **prepends** a flagged sentence to
  any existing notes rather than appending, since it's the first thing
  anyone opening the certificate needs to see; `applyIssueDatePlaceholder()`
  re-parses every row with the rule on or off and restores
  `outcomeIfValid`, never re-querying the database. `CertificateCsvFormat::parseRow()`
  gained an `allowPlaceholderIssueDate` parameter (default `false`, so
  every existing caller is unchanged); confirmed directly that the
  placeholder is applied immediately after the Issue Date cell's own
  malformed-date check and *before* every downstream rule (the Expiry
  Date check, the boolean columns, the survey-date-list validation) —
  so the no-expiry-with-a-survey rule and the "a survey cannot predate
  the certificate" check both judge the placeholder date rather than a
  date the import is about to overwrite. `CertificateImportRow` gained
  `cells` and `outcomeIfValid` (computed unconditionally, before the
  valid/invalid branch, so it stays meaningful even for a currently
  Invalid row); `CertificateImportPreview` gained `vesselId` and
  `columnIndex`, both kept so the preview dialog's checkbox can
  re-evaluate every row without re-reading the file or touching the
  database again. `CertificateImportPreviewDialog`'s
  `placeholderToggled()` reads the user's overwrite ticks back first
  (`applyTicks()`) before the table is rebuilt underneath them, so
  toggling the checkbox never silently resets a decision the user
  already made.
- `CertificateListActions.cpp`, over the `CLAUDE.md` §9 guideline since
  step 12, is **resolved** — down to 224 lines, since
  `CertificateDeleteActions` was split out of it for this round's own
  reasons. `CertificateListWidget.cpp` grew again, 510 → **554 lines**,
  for the header-settings wiring and the `ExtendedSelection` change;
  still the single most over-guideline file in the project (see
  "Outstanding technical debt" below). `CertificateCsvFormat.cpp` (302)
  and `CertificateCsvImporter.cpp` (311), both already over the
  guideline before this round, did not grow enough to need a further
  split — confirmed at these exact counts directly.
- **Verified independently**: every new and changed production file —
  `TableHeaderSettings`, `CertificateSelection`, `CertificateBulkDeletion`,
  `CertificateDeleteActions`, `CertificateIssueDatePlaceholder`, the
  modified `CertificateCsvFormat`/`CertificateCsvImporter`,
  `CertificateImportPreviewDialog`, `CertificateListWidget`,
  `CertificateListActions`, and `CertificateAttachmentActions` — read in
  full against the actual source, including the specific line-count
  claims above. Both `CMakeLists.txt` files confirmed to register all
  six new production files and all three new test suites
  (`tst_CertificateListColumns`, `tst_CertificateBulkDelete`,
  `tst_CertificateImportPlaceholder`), each with its own
  `ship_add_migrations()` call. The three new test files' own content
  was not independently read line-by-line — taken on the strength of
  the reported 43/43 passing suites, since a `CMakeLists.txt` entry
  naming a `.cpp` that doesn't exist is a hard configure error, not a
  silent gap.
- Full clean Debug + Release rebuild and `ctest`, both **43/43
  passing** — the Debug safety-check pass before committing reported
  `ninja: no work to do` (confirming no production code had changed
  since the clean build) ahead of the 43/43, and the subsequent clean
  Release rebuild reported 325/325 targets with zero warnings ahead of
  its own 43/43.
- Committed as **`5bf6905`** (29 files: 14 new, 15 modified; 2261
  insertions, 162 deletions) — body covers each of the three fixes with
  its reasoning, the sort-persistence consequence, the multi-select
  rule, the Attachment-button gap and fix, the no-outer-transaction
  call, and why the placeholder is applied during parsing rather than
  to an already-rejected row.

Two small items raised during this round were resolved directly, not
through Claude Code, since neither is production C++ code:

- The untracked `Claude outputs/` folder (two hand-built CSVs carrying
  real fleet data, ~85KB) was not in `.gitignore` and one `git add -A`
  away from permanent git history. Added directly:
  `Claude outputs/` with an explanatory comment. Confirmed afterwards,
  via `git check-ignore -v` and `git ls-files | grep -ci "claude outputs"`
  (0), that nothing from it is tracked. Committed separately as
  **`7a32168`**, deliberately apart from `5bf6905`, so this piece of
  repo hygiene stays findable on its own rather than buried in an
  unrelated feature commit. Pushed `ad2a62d..7a32168`.
- `docs/certificate-crud-spec.md` §8.2 amended directly to document the
  sort-persistence consequence above, since it's a documentation
  correction rather than a code change — see "Decisions confirmed by
  the developer" below for the exact wording's intent.

Release rebuilt and redeployed after both commits: `build-release/`
deleted outright and reconfigured from nothing (not an incremental
build), 325/325 targets, zero warnings, 43/43 Release suites passing.
`dist/ShipManagement/` refreshed in the order that matters — the fresh
exe copied over the old one *before* `windeployqt --release
--compiler-runtime` ran, not after, confirmed byte-for-byte (`cmp`)
against the build output before deploying. The stale-exe trap from the
first Release build would have bitten again otherwise: what was already
in `dist/` was the 3 Nov exe, 50,521 bytes smaller than this round's.
`ShipManagement.exe` is now **1,307,740 bytes**, dated **2026-11-09
12:23:58** (local time on the dev machine) — confirmed directly from
the file on disk, not taken from the report. 62 files, 70MB total;
`platforms/qwindows.dll`, `sqldrivers/qsqlite.dll`, and
`Qt6PrintSupport.dll` all present. Not launched, for the same §10.1
reason as the first Release build: the live database's real `%APPDATA%`
path is still ambiguous for a process launched from outside a human
double-click. The developer's manual check on the work laptop should
confirm the Name column can be widened and the width survives a
restart, and that selecting two certificates greys out Renew, the
extension button, and Attachment while Delete stays live.

---

### Certificate list number — prefixed codes (`S1`, `P.6`, `EU.9`, …) ✅

Raised by a real bulk import, not a hypothetical: the developer's first
attempt to import V UNO's corrected certificate list, now carrying real
exported ids and the developer's own reference numbers, rejected exactly
60 of 247 rows — every one of them for the identical reason. The
developer's company reference document, the **S-14 Monthly Certification
Status Report** (fleet-wide, not vessel-specific), uses eight prefixed
annexes alongside the plain `1`–`169` the "No." field already handled:
`S1`–`S19` (Statements), `P.1`–`P.48` (Plans and Documents), `EU.1`–`EU.9`
(EU Directives), `US.1`–`US.12` (USA Federal), `USCA.1`–`USCA.4`
(California), `USAL.1`–`USAL.2` (Alaska), `PC.1`–`PC.2` (Panama Canal),
`IS.1`–`IS.2` (Israel). Not every vessel carries every annex, but the
numbering scheme itself is the company's fixed standard.

**Decided with the developer, not assumed:** the sort order groups by
prefix in the S-14 sheet's own section order (a fixed priority list, not
alphabetical — alphabetical would put California before Alaska before
Panama; the sheet doesn't), and scope is the format/sort fix only — no
pick-list or catalogue of the sheet's ~200 entries anywhere in the app.
That second point matters on its own: it keeps this change from quietly
reopening the "no shared `certificate_type` table" decision from step 5,
which was rejected for exactly the reason a catalogue feature would have
revived — different vessels legitimately need different rules for what
looks like the same certificate.

Spec: `docs/certificate-list-number-prefix-spec.md`, cross-referenced from
`certificate-crud-spec.md` §8.2.

Implementation, verified independently against the actual source before
either commit, not taken from the report:

- `CertificateListNumber::pattern()` (`domain/Certificate.h/.cpp`) widened
  from `(?:[0-9]+[A-Za-z]*)?` to `(?:(?:[A-Za-z]{1,6}[.]?)?[0-9]+[A-Za-z]*)?`
  — an optional 1–6 letter prefix, with or without a trailing dot matched
  exactly as the sheet writes it (`S1` has none, the rest do), then digits,
  then optional trailing letters, still fully optional so blank stays
  valid. Shared verbatim by the live `QRegularExpressionValidator` on
  `CertificateEditForm`'s `m_listNumberEdit` — confirmed directly that call
  site needed no edit of its own, exactly as the spec predicted, because
  that sharing is the whole reason the rule lives in the domain.
- `splitListNumber()` rewritten around one anchored regex with three
  captures (prefix, digits, trailing letters), replacing the old
  leading-digit-count loop, which could no longer assume position 0 is a
  digit. The prefix is normalised for comparison only — uppercased, dot
  stripped — so `p.6`, `P6`, and `P.6` are one certificate for ordering
  purposes while all three remain valid to type and are stored exactly as
  written, the same call already made for `3a`/`3A`.
- New `groupOrder()`: the fixed nine-entry list (plain, `S`, `P`, `EU`,
  `US`, `USCA`, `USAL`, `PC`, `IS`) matching the sheet's own section order.
  `groupRankOf()` falls back to "after every known group" for anything not
  on the list, so a ninth annex the sheet doesn't have yet is never lost or
  rejected — it sorts at the bottom until the list is updated, which is the
  one place this spec needs revisiting if that happens. `lessThan()` now
  compares blank → group rank → prefix (the tiebreak two unknown prefixes
  need, since they share one rank) → number → letters.
- `CertificateValidation.cpp`'s error message reworded to describe the
  prefix and give a real example from each shape (`15, 3A, 15D, S1, P.6,
  EU.9`).
- New migration **`011_widen_certificate_list_number_check.sql`** (136
  lines) — migration 004 is already applied and committed and is never
  edited (`CLAUDE.md` §6.6; `MigrationRunner` enforces this itself by
  checksum), and SQLite cannot `ALTER` a `CHECK` in place, so this is the
  standard rebuild: a new table with every column from 003+004 in the same
  order, an explicit column-by-column `INSERT…SELECT` (never relying on
  the two tables happening to agree on column order), drop, rename,
  `ix_certificate_vessel` recreated. **This is the first migration in the
  project that rewrites an existing table rather than only creating one or
  adding a column.** Verified independently that no table references
  `certificate` with a `FOREIGN KEY` — checked `endorsement`,
  `certificate_extension`, and `certificate_attachment`'s own migrations
  directly, all three deliberately omit the `REFERENCES` clause for
  reasons already on record — so the drop-and-rename cannot cascade into a
  child table.
- The new `CHECK` is **deliberately looser than `isValid()`**, not a
  translation of it: SQLite's `GLOB` has no repetition counts and no
  alternation, so "1–6 letters, optional dot, digits, optional letters" as
  one reliable pattern isn't expressible. It checks only what `GLOB` can
  express dependably — letters/digits/dots only, at most one dot, at least
  one digit — the same "defence in depth, the inner layer is the one that
  actually matters" reasoning already used for `certificate.vessel_id`
  having no foreign key. Logically, this is **strictly** looser than the
  old `CHECK`, not merely different: anything the old rule admitted was
  letters-and-digits-only starting with a digit, which necessarily has
  zero dots and at least one digit — so no row stored anywhere, on either
  machine, can be blocked by this migration. That reasoning, not just the
  empirical proof below, is what makes the work-laptop database provably
  safe even though it was never directly inspected.
- **The migration proven twice**, against real data, through the project's
  actual `MigrationRunner` rather than hand-run SQL — not against an empty
  schema either time:
  - Against a throwaway copy of the **dev machine's** live database (743
    certificates, 7 vessels, 37 endorsements): row count unchanged, every
    row identical field-for-field across all 23 columns via `QSqlRecord`
    rather than a named list, column names/order identical,
    `ix_certificate_vessel` recreated, `PRAGMA integrity_check = ok`, no FK
    violations, the new codes all accept, the old rejects (`15-D`, `P..1`,
    `ABC`, `'15 D'`) still reject, and a second run applies nothing
    (checksum no-op). Source file confirmed byte-identical and untouched
    afterward (417,792 bytes, same mtime, before and after); the copy was
    then deleted since it held real fleet data.
  - A **portable proof kit** (`dist/migration-proof-kit/`, deliberately
    under the already-`.gitignore`d `dist/`) built so the developer can run
    the identical proof against the **work laptop's** own database, not
    just take the dev machine's word for it. Two real hazards were caught
    and fixed during the kit's own rehearsal, not left for the developer to
    hit cold: the `.bat` couldn't find its own exe because Windows doesn't
    always search the current directory (fixed with `%~dp0` absolute
    paths), and pointed at an empty or wrong file, the harness printed a
    wall of `FAIL` lines that read as though the migration had destroyed
    something — two guards (no `certificate` table; already at version 11+)
    now stop with one plain sentence and exit cleanly instead. All three
    failure paths plus the no-database-present path were tested before the
    dress rehearsal against a real copy of the dev machine's own database,
    run exactly as the developer will run it, end to end, `.bat` included.
    **Verified independently that the kit's snapshot of migration 011 is
    byte-identical to the committed file** — hashed both copies myself
    (`md5sum`), not taken from the report: `5a5142a9…` on both sides — so
    the kit proves the file that actually shipped, not a stale copy taken
    before the last edit.
- Tests: `tst_CertificateListNumber.cpp` extended with 22 accept rows
  (one real code from every annex, a lowercase prefix, the 6-letter
  boundary, a prefix without its dot) and 17 reject rows, plus six new
  sort functions proving the full worked example from the spec, the
  unknown-prefix fallback, and the unknown-vs-unknown tiebreak.
  `tst_CertificateRepositoryValidation.cpp` gained the same accept/reject
  coverage at the repository level plus two tests writing straight past
  `validateCertificate()` with a raw `INSERT` — one proving the widened
  `CHECK` accepts every real annex code, one proving it still refuses what
  `GLOB` can actually catch — the same role the existing no-expiry
  `CHECK`'s own test already plays. `tst_CertificateCsvFormat.cpp`'s
  invalid-`No.` example was replaced (its old example, a letter before the
  first digit, is now simply a one-letter prefix and is valid) and
  prefixed codes added to the valid set.
- **One existing assertion had to flip from reject to accept, in three
  files, and there's no way around it.** A letter before the first digit
  (the headline invalid case in the original rule) is now structurally
  identical to a one-letter prefix like `S1` — there is no pattern that
  accepts `S1` and refuses it. Moved to the accepted side in
  `tst_CertificateListNumber.cpp`, `tst_CertificateRepositoryValidation.cpp`,
  and `tst_CertificateCsvFormat.cpp`, each with a comment explaining why,
  rather than carving out an exception to avoid the flip.
- **A real mistake in my own spec, caught during implementation, not
  before it shipped.** §5 originally named `"6P"` as a case to keep
  refusing. Wrong: `6P` is plain "digits, then letters" — the ordinary
  `15D` shape — and has always been valid. The invariant that actually
  needs a test is a digit reappearing *after* letters (`3A1`, `1A2B`,
  which would make the split ambiguous), which is what the spec and the
  test suite both assert now. Corrected directly in the spec, not through
  Claude Code, since it's a documentation fix rather than a code change —
  and it took two attempts: the first edit reported success but never
  actually landed on disk, caught only because Claude Code read the file
  fresh before committing it and found the old text still there.
- Full clean Debug + Release rebuild from nothing (both trees deleted and
  confirmed absent first, per `CLAUDE.md` §10.1) and `ctest`, **43/43
  passing in both**, zero configure/build warnings in either.
- Committed as **`8f16dfb`** (10 files: `CMakeLists.txt`, the new
  migration, `Certificate.h/.cpp`, `CertificateValidation.cpp`, the three
  test files, the new spec, and the `certificate-crud-spec.md` pointer;
  +744/−36) — kept separate from **`d9aae0b`**, which is the previous
  round's (usability fixes) `PROJECT-STATUS.md` write-up rather than this
  one, committed first so it doesn't read as buried inside this round's
  feature commit. Both staged-file lists were read back before committing
  — 1 file, then exactly the intended 10 — confirming nothing from
  `Claude outputs/` or `dist/` was swept in either time. **Not pushed**;
  `main` is two commits ahead of the remote, pending the developer's
  go-ahead once the work-laptop proof comes back green.

Production files stayed small: `Certificate.h` 106 lines, `Certificate.cpp`
196, `CertificateValidation.cpp` 68, migration 011 itself 136. No new entry
for the size-debt list below. `CertificateListWidget.cpp` is untouched by
this round and remains the project's single most over-guideline file at
554 lines — still worth its own small round, not bolted onto a feature,
whenever that's next.

---

### Vessel Archive / Restore ✅

The "small follow-up" `vessel-crud-spec.md` §2 deferred back at step 4 —
removing a vessel from the fleet — built now that the certificates module
exists and there is something real to design it against. Spec:
`docs/vessel-archive-spec.md`.

Archive, never delete: `CLAUDE.md` §6 rule 4 bans `DELETE FROM`, so the
only available action is flipping `is_deleted`, and "Archive" is the
honest label for it. Restore was built in the same round rather than
deferred — once `list()` stops returning a vessel, an app with no restore
path would need its database hand-edited to undo a misclick.

**No cascade, and it is now proved rather than argued.** `archive()`
touches exactly one row. Every path to a vessel's certificates goes
through `VesselRepository::list()` or `findById()`, both of which already
excluded archived rows, so the certificates, endorsements, extensions and
attachments go quiet and come back untouched on their own.
`archivingAVesselHidesItsCertificatesFromTheAlertSweep()` in
`tst_Alerts.cpp` drives `CertificateAlertProvider::attentionByVessel()`
directly: two vessels with outstanding certificates, archive one, the
sweep drops to one row, the certificate's own `is_deleted` is asserted
still `0`, and a restore brings the row back with no second write.

Implementation:

- **`core/Vessel.h`** gains `bool isArchived`, backed by the existing
  `is_deleted` column rather than a new one.
- **`core/VesselRepository`** — `is_deleted` appended to `kSelectColumns`
  and read as `value(8)` in `vesselFromRow()`. Appended, never inserted:
  both are positional, and a column added mid-list would shift every field
  after it so that each silently starts reading the column to its right,
  which looks like nothing at all at a glance. `list()`/`findById()` take
  a trailing `includeArchived = false`, so every pre-existing caller keeps
  its exact behaviour with no edit of its own. `list()`'s `WHERE` is now
  assembled from whichever filters apply rather than from a `1 = 1`
  placeholder.
- **`archive()` / `restore()`** refuse outright for `isScopedToOneVessel()`
  — the bare form `create()` uses, deliberately *not* `update()`'s looser
  "scoped and not my own id". A VESSEL installation may not archive even
  its own ship; this is fleet management.
- Both **pre-read the row** (`findById(..., includeArchived = true)`)
  instead of inferring failure from `numRowsAffected()`. `update()` can
  collapse its zero-row case into one message because it has only one
  cause; these have two — an id that isn't there, and a vessel already in
  the state being asked for — and one message would be wrong half the
  time. Four distinct messages result, each pinned by a test.
- **`VesselListWidget`** — "Show archived vessels" checkbox (off by
  default), one context-sensitive `&Archive…` / `&Restore` button
  disabled when nothing is selected, a confirmation on Archive only, and
  `emit vesselsChanged()` on both.

Judgment calls where the spec left the choice open, and one where it
offered an option that does not work:

- **Archived rows are greyed and italic, not `setEnabled(false)`.** The
  spec offered disabling the row's items as one of two acceptable
  mechanisms. It is not usable: a disabled `QTableWidgetItem` cannot be
  selected, and an archived row that cannot be selected is one whose
  Restore button can never be reached. Greyed via the palette's disabled
  text colour plus italic, which leaves the row selectable.
- **The Status column appears only while the toggle is on.** A column
  reading "Active" on every row in the default view is noise, so
  `setColumnHidden()` ties it to the checkbox: the normal fleet list looks
  exactly as it did, and ticking the box adds an unambiguous word rather
  than relying on colour alone.
- **No ellipsis on `&Restore`.** An ellipsis promises a further question;
  Restore deliberately asks none.
- `editVessel()` checks `isArchived` off the row's own item data **before**
  calling `findById()`, and says "This vessel is archived. Restore it to
  make changes." Letting it fall through would hit `findById()`'s default
  exclusion and produce "This vessel is no longer in the database" about a
  vessel sitting visible on screen.

**A prediction in the spec turned out to be wrong, and the test now says
so.** §6 states that archiving frees a vessel's IMO number for reuse, so
that a later restore could fail the uniqueness check. It cannot.
`migrations/002` declares `imo_number TEXT NOT NULL UNIQUE` — a plain
table constraint with no `is_deleted` condition — so an archived vessel
keeps its number and nothing can take it meanwhile. The right answer in
domain terms too: an IMO number belongs to a hull for life and is never
reassigned. `anArchivedVesselStillHoldsItsImoNumber()` pins the real
behaviour. `restore()`'s IMO check is kept as an unreachable backstop,
commented as such, because it costs one query and becomes load-bearing
the day that `UNIQUE` is ever relaxed to a partial index.

The one live consequence: because `isImoNumberTaken()` filters to
`is_deleted = 0` while the constraint does not, trying to add a *new*
vessel reusing an archived one's IMO number now fails with the raw
`UNIQUE constraint failed: vessel.imo_number` rather than the friendly
"This IMO number already belongs to another vessel." Before this round
the two could not disagree, because nothing could set `is_deleted`. Noted
under outstanding debt; the one-line fix is dropping the `is_deleted`
filter from `isImoNumberTaken()` so the check agrees with the database,
and it is the developer's call because it changes a documented rule.

Tests — 43/43 suites pass, Debug and Release, 0 compiler warnings:

- `tst_VesselRepositoryScope.cpp` (10 passes): archive and restore refused
  in VESSEL mode, including the installation's own vessel, with the row
  verified still active afterwards.
- `tst_VesselRepositoryWrites.cpp` (18): the flag set, revision bumped,
  creation columns untouched, row still present; all four failure messages
  distinct; restore round-trip with revision reaching 3; the IMO finding
  above; and the default-versus-opt-in behaviour of both reads.
- `tst_VesselSelectorRefresh.cpp` (9): archiving the vessel currently on
  screen leaves `CertificateListWidget::vesselId()` empty via the existing
  `refreshVesselSelector()` path, and a second test drives the real button
  — select row, press Archive, answer the modal Yes — to cover the wiring
  rather than just the repository.
- `tst_Alerts.cpp` (16): the no-cascade proof described above.

The `emit vesselsChanged()` on archive was vacuity-checked: commenting it
out makes `archivingAVesselRemovesItFromTheSelector()` fail, and the file
was restored byte-identically afterwards. That signal is the only thing
keeping the certificates toolbar in step with the fleet
(`certificate-crud-spec.md` §8.1 is the defect report from the last time
something changed the fleet without it).

Sizes: `VesselRepository.cpp` is now **522 lines** and
`VesselListWidget.cpp` **319**, both over `CLAUDE.md` §9's ~300. See the
debt list below.

---

### Vessel archive follow-up — IMO message, and two files over the guideline ✅

The two loose ends the first round left, written up as
`vessel-archive-spec.md` §10 and built together.

**§10.1 — the IMO check now agrees with the database.** The first round
surfaced a real divergence: `isImoNumberTaken()` filtered to
`is_deleted = 0`, while migration 002's `imo_number TEXT NOT NULL UNIQUE`
has no such condition. Before archiving existed the two could never
disagree. Afterwards they could, and the result was that adding a vessel
reusing an archived one's number passed the application's check and was
then refused by SQLite as raw `UNIQUE constraint failed: vessel.imo_number`.

The check stops filtering on `is_deleted`, so it sees exactly what the
constraint sees. It is also no longer a bare yes/no:
`findImoNumberHolder()` returns who holds the number and whether they are
archived, because the message depends on it:

- Active holder — unchanged wording. Naming a vessel the user can already
  see in the list tells them nothing.
- Archived holder, from `create()` — *"This IMO number belongs to the
  archived vessel 'X'. Tick "Show archived vessels" and restore it instead
  of adding it again."* An archived vessel is invisible by default, so a
  generic message would send someone hunting for a ship that is not in the
  list.
- Archived holder, from `update()` — the same sentence without the restore
  hint. Someone correcting a vessel's details is not trying to add one, so
  Restore is not the answer to their problem.

That is also the right answer in domain terms, which is what settled it:
an IMO number belongs to a hull for life and is never reissued, so the
database was right and the application was wrong.

`restore()`'s own IMO check stays as a backstop and now cannot fire for
two reasons rather than one — the `UNIQUE` constraint, and the fact that
the only row the check could match is the vessel being restored, which
`excludeId` removes. Kept because it costs one query and becomes
load-bearing the day that constraint is relaxed to a partial index.

**§10.2 — file sizes.**

`VesselListWidget.cpp` **319 → 220 lines**, under the guideline.
`VesselArchiveActions` is split out along the same seam
`CertificateDeleteActions` was, and owns the button, its label and
enabled state, the confirmation, and both actions. The widget keeps the
table, the reload and the edit dialog.

The id and archived-flag item roles are now declared once in
`VesselArchiveActions.h`, since the widget writes them and the actions
class reads them, and a disagreement would have been silent. The widget's
`Column::Name` is *defined from* the shared column constant rather than
repeating `0`, so the column carrying the roles cannot drift from the
column the screen puts them on.

`VesselRepository.cpp` **522 → 557 lines — still over, and it went up.**
Reported rather than fixed by splitting the class across files, as §10.2
asked. `archive()` and `restore()` do now share
`checkArchiveChangeAllowed()` (empty-id guard, OFFICE-only refusal,
pre-read, already-in-that-state check) and `writeArchivedFlag()` (the
`UPDATE`, the `numRowsAffected()` backstop), with every message derived
from the direction rather than passed in, so the two cannot drift apart
in wording. `archive()` is down to ten lines. But §10.1 landed in the
same file in the same round and added more than the deduplication
removed — `findImoNumberHolder()` is larger than the `isImoNumberTaken()`
it replaced, and `imoNumberConflictMessage()` is new. Of the 557 lines,
**394 are code**; 87 are comments and 76 blank.

Getting this file under ~300 needs extracting a real collaborator rather
than more deduplication — the IMO-conflict logic is the obvious
candidate, at roughly 75 lines and genuinely separable — and that is a
decision worth making deliberately rather than at the end of a feature
round. Carried in the debt list.

**Verification.** Clean Debug and Release rebuilds, 0 warnings, 43/43
suites. `tst_VesselRepositoryWrites` is at 19 passes: the reworked
`anArchivedVesselStillHoldsItsImoNumber()` now asserts the error names
the archived vessel, suggests restoring it, and does **not** contain
`UNIQUE constraint`; the new `updateRejectsAnArchivedVesselsImoNumber()`
asserts the same minus the restore hint. The pre-existing duplicate-IMO
tests for active vessels pass unchanged, which is what confirms §10.1
changed only the archived case.

The `emit vesselsChanged()` vacuity check was repeated after the
extraction, because moving code is exactly how that signal gets lost:
removing it makes `archivingAVesselRemovesItFromTheSelector()` fail, and
the file was restored to the same md5 afterwards. The button also kept
its position in the layout — the test that finds it by index passed
without being touched.

---

### Annual surveys — the latest survey counts ✅

`docs/survey-latest-counts-spec.md`. Not a feature: a correction to how
the annual track is read, found on a real certificate.

**The bug, on V TRE's Load Line certificate.** Issued 29 May 2022,
expires 09 Mar 2027, so its anniversaries are 09 Mar 2024, 2025 and 2026.
One annual endorsement recorded — 12 Apr 2026, Iskenderun. The list
showed it **Overdue, −519 days**, against a window that had closed on
09 Jun 2025.

`SurveySchedule::evaluateAnnual()` walked the anniversaries in order and
gave each one the earliest endorsement dated on or after its own window
opened. The 2026 survey was therefore claimed by the **2024**
anniversary, counted as a late survey for it, and 2025 was left with
nothing to satisfy it. The warning only cleared once the developer had
entered every past annual date.

That is the wrong question to ask of this office's data. The company's
S-14 report records the **last** annual survey, not the full history, and
that is what gets entered.

**The rule now.** The latest annual survey covers its own anniversary and
every anniversary before it; only anniversaries after it remain. Which is
what `certificate-control-spec.md` §3.3 always said — *"the system always
looks forward from the latest endorsement of the matching survey type"* —
and what the claim-walk added to §4.5 for late surveys had drifted away
from. This round puts the code back on §3.3.

Concretely: find the last anniversary whose window had opened by the day
the latest survey was done, treat it and everything before it as
satisfied, and start tracking at the one after. A survey dated before even
the first window opens still covers nothing, unchanged. A survey done
after its own window closed still counts, and is still recorded as late in
`reason` — but only for the anniversary it actually covers, so there is at
most one late note instead of one per anniversary.

**The accepted trade-off, recorded deliberately.** Once a later annual
survey is recorded, a genuinely missed earlier one is no longer flagged.
The developer chose this knowingly: in practice a missed annual is dealt
with by the flag or class at the time, and the office tracks forward from
the last survey. `aMissedEarlierAnnualIsNotReportedOnceALaterOneExists()`
pins it, with a comment saying so, so nobody "fixes" it later.

**What did not change.** The `endorsement` table, its data, the
Endorsements list and the endorsement UI — the full history is still
recorded and still shown, only the evaluation changed. No migration, no
schema change. The intermediate track is untouched (one survey per term,
already counting anything on or after its window opens), as are
extensions, renewal dating and expiry severity. `REPLACES_ANNUAL` still
contributes its intermediate candidates exactly as before.

This also settles the "endorsement model rework" question deferred since
step 13 — whether to keep only the most recent survey per track. The
answer is: keep the full history, schedule from the latest.

**Verification.** Clean Debug and Release rebuilds, 0 warnings, 43/43
suites. `tst_CertificateStateSurvey` is at 18 passes, three of them new:
the V TRE case with its real dates, the single late note naming only the
anniversary it covers, and the trade-off test above.
`eachAnniversaryClaimsTheEarliestEndorsementAvailableToIt` kept passing
but its name described the rule that had just been removed, so it is now
`theLatestSurveyCoversEveryAnniversaryBeforeIt`.
`anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing` passes unchanged.

The three new tests were run against the old algorithm before the change
was kept, and all three failed — the V TRE one with the §1 bug reproduced
word for word: *"Survey overdue: the window for 09 Mar 2025 closed on
09 Jun 2025. The 09 Mar 2024 survey was completed late, on 12 Apr 2026."*

**One existing assertion had to change**, in
`tst_CertificateCsvImportSurveys`. The old
`asingleMostRecentDateWouldHaveReportedTheSameShipOverdue()` existed to
justify the multi-date `Annual Surveys` CSV column: it showed that
importing only the most recent date produced `Overdue`, because the
claim-walk gave that one date to the oldest anniversary. Under the new
rule the two readings agree — one date and the full history both say
`NotDue` — so the assertion was asserting the bug. Rewritten as
`aSingleMostRecentDateNowAgreesWithTheFullHistory()`, which asserts that
agreement and records why the column stays regardless: the history is
itself the record, shown in the Endorsements list, the PDF statement and
the audit trail, and an import that kept only the last date would lose it
for good. Nothing else in the suite needed touching — including
`tst_CertificateListStatus`, `tst_CertificateRenewal` and `tst_Alerts`,
which the spec flagged as possible casualties.

**File sizes.** `SurveySchedule.cpp` 178 → 200 lines, well under the
guideline; the new algorithm is shorter than the walk it replaced, and
the growth is the comment explaining why it changed.
`tst_CertificateStateSurvey.cpp` is 316 → **426 lines** and
`tst_CertificateCsvImportSurveys.cpp` 417 → **429**, both over ~300 and
both already over before this round. Reported, not split: the obvious
seam is to move the five matching-order cases into a
`tst_CertificateStateSurveyMatching` of their own, which is a
housekeeping decision rather than part of this fix. Carried in the debt
list.

---
---

### Housekeeping — file sizes, the chooser's last folder, two small fixes ✅

`docs/housekeeping-round-spec.md`. Round 2 of closing out Certificate
Control. No behaviour change anywhere except §5, and every existing test
passes as it stands — the only test edits are moves.

**§2 — `VesselRepository.cpp` 557 → 430 lines.** Two helpers, both in
`core/`, both used only by this repository, which is what `CLAUDE.md` §9
allows for a helper holding SQL.

`VesselImoNumberCheck` (57 + 79) took `findImoNumberHolder()`,
`imoNumberConflictMessage()` and the holder type — the ~75-line piece the
last round named as the obvious candidate. `create()`, `update()` and
`restore()` call it; every message is word for word what it was.

`VesselArchiveWriter` (45 + 132) took the archive pair. §2.2 made this
conditional on the file still being well over ~300 after the first move;
it was, at about 495, so it was done. The one judgment inside it: the
pre-read used to go through `VesselRepository::findById()`, which reads
the whole positional column list. The helper reads `imo_number,
is_deleted` instead, so the one place that column list lives stays the
one file whose queries use it. That read has no vessel-scope clause and
none is missing — the OFFICE-only refusal above it means an OFFICE
installation is the only one that ever reaches it, and its scope filter
is empty. Its SQL-failure message is the same string `findById()` used,
so even that unreachable path reads identically.

**Still 430, and that is where it stops.** 307 of those lines are code.
What remains is `list`, `findById`, `validate`, `create`, `update`,
`setAttachmentRootPath`, `archive`, `restore` and the row mapping, which
is the class. The one other separable piece is the anonymous namespace at
the top (`kSelectColumns`, `vesselFromRow()`, the null-handling helpers),
and moving it would be actively worse: that file's own warning comment is
about keeping the column list and the queries that read it positionally
in step, and a file boundary between them is exactly what the comment
warns against. §1 says report the number and stop rather than invent a
seam, so that is what this is.

**§3 — `CertificateListWidget.cpp` 554 → 379 lines.** Two splits, not one.

`CertificateRowRenderer` (76 + 127, `ui/`) took `fillRow()`, the
attachment glyph, `expiryLabel()` and the `static_assert` tying the row
data to its column — the same seam `CertificateListActions`,
`CertificateIoActions` and `CertificateDeleteActions` were cut along. The
`Column` enum and the column count moved into its header, because the
renderer writes the cells while the widget labels, sizes and sorts them,
and a disagreement about which column is which would be silent. The
widget aliases it back to `Column` locally, so not a line of the
remaining code had to change.

That left 441, still well over, so §3.2's "choose the next split
yourself" applied. **I did not take the constructor's layout code**,
which §3.2 suggested. A class whose job is to call `addWidget()` twelve
times would need the table, seven buttons and a checkbox handed to it —
ten parameters for no decision of its own, which is §1's "helper with no
real boundary is worse than a long file".

What I took instead was the first half of `reload()`, into
`CertificateListRows` (71 + 102) — and into `service/`, a layer down,
rather than another `ui/` helper. That code reads the alert thresholds,
turns a core `AppSetting` into this module's `AlertThresholds`, looks up
every certificate's endorsements and active extension, runs
`computeCertificateState()` and drops the rows the "needs attention"
filter excludes. Every one of those is a decision about what a value
should be, which `CLAUDE.md` §4 rule 3 says a widget may not make: a
widget may format and display a value, not decide it. So the remaining
oversize was partly code that had been in the wrong layer since step 7,
and §9's own instruction is to split along layer boundaries first. The
widget now reads today's date — still the one legitimate place in the
chain — asks the service for the vessel's rows, and draws them. It also
no longer holds an `AppSettingRepository` reference at all.

`CertificateAlertProvider` does the same gathering for the sidebar badge,
over the fleet instead of one vessel, and both files already carry
comments saying the two must agree. Making them share this class is the
obvious next move and is deliberately **not** attempted here: the loops
differ in shape and in warning text, and changing what the badge counts
does not belong in a housekeeping round. That is written into the new
class's header so it is not lost.

**Still 379** (244 code). Stopping there, per §6. The remaining bulk is
one 125-line constructor, and the argument against splitting it is the
one above.

The three list suites — `tst_CertificateListWidget`,
`tst_CertificateListStatus`, `tst_CertificateListColumns` — pass
untouched, which is §3's stated proof that the screen looks exactly as it
did.

**§4 — the survey suites.** `tst_CertificateStateSurvey.cpp` 426 → 285,
with the five matching cases in a new
`tst_CertificateStateSurveyMatching.cpp` (165). **16 test functions
before, 11 + 5 = 16 after** — the two suites report 13 and 7 passes, each
counting its own `initTestCase` and `cleanupTestCase`. The project now
has 45 suites.

`tst_CertificateCsvImportSurveys.cpp` (430) was **left alone**, per §4's
"split only if a clean seam exists". There is none: all nine cases run on
one 66-line fixture — a `QTemporaryDir`, a database connection,
`importOneRow()` and `endorsementCountFor()` — so any cut either copies
that fixture into a second file or needs a new shared support header, and
the cases all answer the one question of what the two survey-list columns
do. 300 of its 430 lines are code.

**§5 — the attachment chooser remembers the last folder.** The one
visible change in the round. After a successful pick it stores a folder
in `QSettings` under `ui/attachmentChooser/lastFolder`: for a file the
folder holding it, for a folder of documents that folder's *parent*, so
the folder just chosen is visible among its neighbours next time rather
than shown from the inside. One remembered folder, shared by both
dialogs. `QSettings` and not `app_setting`, for the reason
`TableHeaderSettings` already gives — which folder this machine's
documents sit in means nothing on the other side of a sync.

It falls back to Documents when the remembered folder no longer exists
(renamed, deleted, a stick pulled out — Qt would otherwise drop to
whatever directory the application was started from), and when it is the
vessel's own attachment folder or inside it. That second case keeps the
safety reason the old comment gave: the managed folder is the one place a
source must never be picked from, because copying a file onto itself only
ever goes wrong. It can arise in ordinary use — the moment a vessel is
pointed at a folder documents were once picked from.

The "inside" test compares cleaned absolute paths and requires a whole
path segment, so `fleet/ships-scans` does not count as inside
`fleet/ships`. It compares **case-insensitively on every platform**,
which is the stricter reading rather than the accurate one. On Windows it
is simply correct; on Linux it can call a folder "inside" when it is not,
and the cost of that is opening at Documents, no worse than a first run.
The mistake the other way round would open the dialog inside the managed
folder.

Plumbing: the chooser is a widget-layer helper with no repositories, so
the vessel's folder is passed in.
`VesselAttachmentRoots::configuredRoot()` and
`CertificateAttachmentService::vesselAttachmentRoot()` are the new
accessors; `CertificateAttachmentActions` and `AttachmentSourceField`
supply it. The field takes it as a **required** constructor argument,
not a defaulted one, so a future dialog cannot forget it —
`CertificateEditDialog` and `CertificateExtensionDialog` were both
updated, and the edit dialog now keeps the vessel id it was already
being given.

`tst_AttachmentChooserFolder` (172, nine cases) tests the decision as a
function: nothing remembered → Documents; remembered and still there →
it; deleted → Documents; the vessel's folder, and a folder inside it →
Documents; a similarly named neighbour → still allowed; a file remembers
its folder; a folder remembers its parent; and an empty path remembers
nothing, which is the guard that makes Cancel harmless. It writes to a
throwaway `.ini` per test through a new
`AttachmentChooser::setSettingsFileOverride()`, the same override
`TableHeaderSettings` has and for the same reason (`CLAUDE.md` §10.1).
The modal dialog itself is not driven, which §5 says it needn't be.

Vacuity-checked: disabling the managed-folder guard makes exactly the two
tests that assert it fail, and the file came back to the same md5.

**§6 — the two small fixes.** `TransactionScope::commit()` now clears its
"open" flag only after `QSqlDatabase::commit()` has actually succeeded.
Before, a failed commit left the transaction open on the connection with
the destructor no longer willing to roll it back, so the next statement
on that connection would have run inside it. Never seen in practice — the
one caller treats a failed commit as a failed import and stops — but the
destructor is the entire point of the class. And `MainWindow.cpp` now
includes `<QSignalBlocker>` directly instead of relying on a transitive
include.

**§7's dropped item** is recorded as dropped: the first-run wizard cannot
hit a duplicate IMO number, because it only runs when there is no
installation row and vessel rows only exist after one has been set up.
Struck from the debt list rather than carried.

**Every file touched, before and after.** Over the ~300 guideline in bold.

| File | Before | After |
|---|---|---|
| `core/VesselRepository.cpp` | 557 | **430** |
| `core/VesselRepository.h` | 125 | 96 |
| `core/VesselImoNumberCheck.h` / `.cpp` | new | 57 / 79 |
| `core/VesselArchiveWriter.h` / `.cpp` | new | 45 / 132 |
| `certificates/ui/CertificateListWidget.cpp` | 554 | **379** |
| `certificates/ui/CertificateListWidget.h` | 121 | 129 |
| `certificates/ui/CertificateRowRenderer.h` / `.cpp` | new | 76 / 127 |
| `certificates/service/CertificateListRows.h` / `.cpp` | new | 71 / 102 |
| `certificates/ui/AttachmentChooser.h` / `.cpp` | 20 / 34 | 49 / 158 |
| `certificates/ui/AttachmentSourceField.h` / `.cpp` | 46 / 74 | 52 / 78 |
| `certificates/ui/CertificateAttachmentActions.cpp` | 216 | 220 |
| `certificates/ui/CertificateEditDialog.h` / `.cpp` | 85 / 218 | 89 / 220 |
| `certificates/ui/CertificateExtensionDialog.cpp` | 130 | 132 |
| `certificates/service/CertificateAttachmentService.h` / `.cpp` | 157 / 224 | 163 / 229 |
| `core/VesselAttachmentRoots.h` / `.cpp` | 95 / 330 | 103 / **342** |
| `core/TransactionScope.h` | 58 | 70 |
| `app/MainWindow.cpp` | 322 | **323** |
| `tests/…/tst_CertificateStateSurvey.cpp` | 426 | 285 |
| `tests/…/tst_CertificateStateSurveyMatching.cpp` | new | 165 |
| `tests/…/tst_AttachmentChooserFolder.cpp` | new | 172 |
| `CMakeLists.txt` | 238 | 243 |
| `tests/CMakeLists.txt` | 133 | 138 |

Two of those were already over the guideline before this round and grew
by the smallest amount that would do the job: `VesselAttachmentRoots.cpp`
330 → 342 for `configuredRoot()`, and `MainWindow.cpp` 322 → 323 for the
one include. Neither was split — the brief named specific files, and
widening it to a file that needed one line would have been scope creep.
They stay on the debt list.

**Verification.** Clean Debug and Release rebuilds, 0 warnings, **45/45
suites** in each.


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
  **Updated by step 13.** CSV import (renumbered to step 13) turned out
  not to be the bypass this worried about: `CertificateCsvImporter`
  issues no SQL of its own, and every write goes through
  `CertificateRepository::create()`/`update()`, which still runs
  `checkVesselAllowed()` on every call. The vessel on a row is also
  always the one selected on screen, never read from the file, so a
  crafted `Vessel IMO` or `Id` cell cannot aim a write at another
  vessel's data even without a database-level FK. The gap an FK would
  still close — nothing below the repository layer enforces it — remains
  exactly as described above; it's just that step 13 didn't turn out to
  be a path that reaches below that layer. The sync engine, whenever it
  arrives, is still the next real candidate to revisit this against.
- The "No." field (`list_number`) is a single free-typed box with a live
  validator, not two separate boxes for number and letter — tried, rejected
  by the developer as fiddly to fill in during data entry. The format
  restriction (digits, then letters) is what makes a single box safe: it
  guarantees the value can always be split unambiguously for sorting.
  **Updated by the list-number-prefix round.** The restriction is wider
  now — an optional short letter prefix, with or without a dot, ahead of
  the digits — to match the developer's own company-wide reference
  numbering (`S1`, `P.6`, `EU.9`, and six more annexes). The single-box
  reasoning above is unchanged; only what the box accepts grew.
- Certificates sort by group before they sort by number: the plain numbers
  first, then each of the company reference sheet's eight prefixed annexes
  together, in that sheet's own section order — not alphabetically. The
  developer chose this explicitly over the simpler, zero-maintenance
  alphabetical alternative, so the list's shape matches the paper form
  it's replacing rather than an arbitrary (if tidier) ordering.
- No pick-list or catalogue was built from the developer's S-14 reference
  sheet, by explicit choice — every certificate on every vessel is still
  typed in by hand. Widening what the "No." field accepts was judged
  worth doing on its own; building a shared list of the sheet's ~200
  entries to choose from was judged a separate, bigger feature, and one
  that would reopen step 5's "no shared `certificate_type` table"
  decision rather than sit quietly alongside it.
- The certificate list's default sort changed from Name to `list_number`
  ascending. This matters to the developer beyond convenience — the fleet
  refers to certificates by this number in day-to-day communication (e.g.
  "Certificate 15D") specifically so the full name doesn't have to be
  typed out, so the list needs to read in that order without the user
  having to click anything.
  **Updated by the usability fixes round.** This is now the *first-run*
  default only. `TableHeaderSettings` persists the header's whole state,
  including the sort indicator, the same way it persists column widths —
  so the moment a user sorts by a different column, that is what they
  get back on this computer next time, until they sort it again. Decided
  deliberately: "how I last arranged this list" is one setting, not two,
  and the alternative (re-forcing `list_number` ascending every launch
  regardless of what the header remembers) would make widths persist but
  sort not, which is the more inconsistent of the two options. `docs/certificate-crud-spec.md`
  §8.2 has been corrected to describe this.
- Deleting more than one certificate at once is possible
  (`ExtendedSelection` on the certificate list), but every other
  selection-driven action — Renew, the extension button, Attachment —
  stays single-certificate by treating "more than one row selected" as
  no selection at all, the same as zero. One rule
  (`CertificateSelection::singleRow()`), not a separate check at each
  button.
- A bulk delete runs as a best-effort loop with **no** outer transaction
  around it — each certificate's own `softDeleteWithChildren()` already
  transacts on its own, SQLite has no nested transactions, and the
  developer's preference is that one certificate failing should not
  silently block deleting the rest of a batch.
- The scratch folder a hand-built CSV import is drafted in
  (`Claude outputs/`, at the repo root) is explicitly **not** tracked by
  git — it may hold real fleet data, and the project's `git add -A`
  habit means anything not ignored is one commit away from being
  permanent. Added to `.gitignore` with a comment explaining why, rather
  than deleting the folder or its contents.
- **Superseded by addendum 2.** Endorsements were add-only through step 6;
  attachment addendum 2 added edit (an UPDATE onto the same row) and
  delete (soft-delete), reached from the certificate's own Endorsements
  section via Edit/Delete buttons alongside Add.
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
- The alert banner shows one row per vessel with something outstanding,
  each with its own View button, rather than a single fleet-wide count —
  settled after the simpler version turned out to have a real gap: with
  one ambiguous count, a click had nowhere honest to land in OFFICE mode
  once more than one vessel could be responsible for it. VESSEL mode
  reuses the identical row logic; it simply never has more than one row.
- Native OS toast notifications are out for now in favour of an in-app
  banner — cross-platform behaviour for `QSystemTrayIcon` varies too much
  across Windows/macOS/Linux (notably macOS's notarization requirement) to
  be predictable for a beginner-maintained cross-platform build. Revisit
  once the app is actually being built and tested on all three platforms.
- The sidebar badge and the alert banner use the identical severity rule
  the certificate list's own "needs attention" filter already uses
  (`DisplayStatus != Valid`) — deliberately one definition of "needing
  attention" across the whole app, not three that could quietly drift
  apart.
- Renewal edits the same certificate row in place. There is no new row, no
  link between an old and new certificate, and `previous_certificate_id`
  (left absent since step 5 "for exactly this reason") turned out to have
  no reason after all — it stays permanently unused. The module's job is
  tracking current certificates, not preserving a history of past ones.
- A renewal clears all of the certificate's recorded endorsements. This is
  not required for correctness — `SurveySchedule::anniversariesOf()` was
  already correctly anchored to `expiryDate`, not `issueDate`, since step 6,
  so a fresh schedule is computed automatically the moment new dates are
  saved, and old endorsements would simply be ignored as predating the new
  cycle's first window either way. The clear happens purely because the
  developer doesn't want old survey history kept once a certificate has
  moved on to its next cycle.
- File/document attachment during renewal (adding a new file, archiving the
  old one) is explicitly out of scope for step 10 — no file storage exists
  anywhere in the app yet. Deferred to the future Document Management
  module.
- Renewal is reached through a dedicated **Renew** button, kept separate
  from the existing double-click-to-edit action, so an ordinary edit can
  never accidentally trigger the endorsement-clearing side effect.
- A short-term extension is a **separate record**, not an edit to the
  certificate — the opposite shape from renewal. The certificate's own
  `issue_date`/`expiry_date` are never read from or written to by
  anything in the extension flow; only the extension's own expiry date
  governs what the list shows and what the alert system counts while one
  is active.
- While an extension is active, the certificate's own survey schedule is
  genuinely suppressed, not just hidden — a short-term certificate is
  assumed to always be too short-lived to have its own annual/intermediate
  cycle, so there is nothing real to evaluate during that window.
- Renew is disabled while an extension is active, since renewing starts a
  fresh cycle and that doesn't fit while the problem the extension bridges
  is still open.
- Resolving an extension **soft-deletes** it rather than clearing it like
  an endorsement on renewal — unlike an internal survey stamp, this is a
  real document from an outside authority, worth keeping even though no
  screen browses that history yet.
- A certificate's own attachment and an extension's own document are
  **two independent mechanisms**, never shared — correcting my own first
  proposal to merge them. An extension never touches the certificate's
  real document, mirroring step 11's "an extension never touches the
  certificate's own dates."
- Attaching a whole folder works identically to attaching a single file
  (same naming, same archive-on-replace, same "open" action) — the
  folder itself is renamed to the generated name; files inside it keep
  their own names untouched.
- No file-type restriction on attachments, and no plan to add one —
  explicitly declined by the developer as unnecessary complication.
- An attachment is reachable from both the certificate list (a
  standalone Attachment button) and folded into the renewal and
  extension dialogs, rather than only one of the two.
- **Superseded by addendum 2.** Step 12 through the first addendum had no
  "remove attachment entirely" action — every path was "replace," never
  "clear to empty" (see `certificate-attachment-spec.md` §10). Addendum
  2 added Delete Attachment and Delete Certificate, both a genuine file
  delete with **no** archive copy — deliberately the opposite of
  replace, since a delete exists to correct a mistake rather than to
  record a supersession worth a trail.
- A vessel's attachment folder is a **hard requirement**: with none
  configured, every attach entry point refuses outright rather than
  falling back to anywhere else, including the old installation-wide
  folder. The developer's own reasoning: every vessel needs its own real
  folder in the office regardless of how it gets there technically (a
  synced Google Drive folder was named as one future possibility, with
  no code written toward it yet — the folder chooser is generic on
  purpose).
- Changing a vessel's attachment folder **moves its existing documents**
  to the new location rather than leaving them behind — explicitly
  preferred by the developer over the simpler "just point at the new
  folder" option.
- The attach control in the certificate dialog is offered in **both**
  Add and Edit, not Edit/Renewal alone — the document is just as often
  already in hand when a certificate is first entered as it is at
  renewal time.
- The Document column indicator is a **plain icon only** — no filename
  shown — doubling as both "something is attached" and "click here to
  open it," reached by a single click with no row selection needed
  first.
- Issue Date returns to the certificate list, placed directly after
  Name — it had left the screen at step 7 in favour of a "what needs
  attention" focus, and comes back now for exactly the per-vessel-folder
  cross-check use case the developer described: receive or renew a
  certificate, check its status, and decide when to act.
- The vessel's attachment folder is now **flat** — no `attachments/`
  wrapper, no per-certificate subfolder, no `extensions/` subfolder.
  Everything the vessel has attached sits directly in the folder the
  developer chose, told apart by filename alone. One subfolder survives:
  a single shared `archive/` per vessel, not one per certificate —
  explicitly confirmed after an early misunderstanding on my side (I
  first read "no archive folder should be" as "stop archiving
  entirely"; the developer meant "no archive subfolder *per
  certificate*," i.e. keep archiving, just flatten where it lives).
- A vessel's folder is now claimed by a hidden `.ship-management-vessel`
  marker file rather than by its structure (there is no longer a
  structural difference to check once the layout is flat). The marker
  moves with a `changeRoot()` but is explicitly excluded from the
  documents that move with it.
- An attached file's name now carries whichever of the certificate's
  expiry date, its next annual survey window, and its next intermediate
  survey window is **soonest** — so the filename itself shows what needs
  attention next, even looking at the files directly rather than at the
  app. Computed once, at the moment of attaching; the schedule moving on
  afterwards does not rename a file already on disk.
- Deleting a certificate removes the real files the database still has
  live paths for, but deliberately does **not** sweep the shared
  `archive/` folder for that certificate's older, already-archived
  copies — accepted as a permanent limitation, not a gap to close later,
  because once a file is renamed into the shared archive there is no
  reliable way back from it to the certificate it came from.
- Deleting a certificate or an attachment is a genuine file delete with
  no archive copy, deliberately the opposite of what replacing a
  document does — a delete exists to correct a mistake, not to record a
  supersession worth a trail. The database rows are still only ever
  soft-deleted, never hard-deleted, same as everywhere else in the app.

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
  - **Updated by the vessel archive round: it grew, to 522 lines**, which
    is the "later step" that entry was watching for. The awkward part is
    that §9's two rules now pull against each other here — the natural cut
    is reads from writes (the way the *tests* are already split into
    `Scope` and `Writes`), but that would put one class across two files,
    which §9 also forbids. Options are a `VesselReadRepository` /
    `VesselWriteRepository` pair, a private implementation struct, or
    accepting the size for a class that is genuinely one cohesive thing.
    A decision worth making deliberately rather than at the end of a
    feature round.
  - **Updated again by the archive follow-up round: 557 lines**, up from
    522 despite `archive()` and `restore()` now sharing
    `checkArchiveChangeAllowed()` and `writeArchivedFlag()`. §10.1's
    `findImoNumberHolder()` and `imoNumberConflictMessage()` landed in the
    same file in the same round and added more than the deduplication
    removed. 394 of those lines are code, the rest comments and blanks.
    Further deduplication will not close a 250-line gap; the next real
    move is extracting a collaborator, and the IMO-conflict logic (~75
    lines, genuinely separable, no dependency on the rest of the class) is
    the obvious one. Still not split across two files, per §10.2.
- ~~`src/app/VesselListWidget.cpp` is 319 lines after the vessel archive
  round~~ — **resolved by the archive follow-up round.**
  `VesselArchiveActions` was extracted exactly as predicted here, and the
  widget is back to **220 lines**.
- ~~`VesselRepository::isImoNumberTaken()` filters to `is_deleted = 0`,
  but `migrations/002` declares `imo_number TEXT NOT NULL UNIQUE` with no
  such condition~~ — **resolved by the archive follow-up round
  (`vessel-archive-spec.md` §10.1).** The check no longer filters archived
  rows, so it sees what the constraint sees, and it now names the archived
  vessel holding the number rather than letting SQLite's raw constraint
  text reach the user.
- `modules/certificates/data/CertificateRepository.cpp` is now ~310 lines,
  over the same guideline, having grown past it while adding the `No.`
  field's handling. Same call as `VesselRepository.cpp`: no obvious seam,
  not split now, but this is the file step 6 (endorsements) will touch
  next, so it's the one to watch most closely.
- `tests/modules/certificates/tst_CertificateRepositoryValidation.cpp` is
  now ~301 lines, also just over the guideline, for the same reason (the
  new `list_number` validation tests). Same call: not split now.
- ~~The first-run wizard (step 3) still lets a duplicate IMO number fail
  with the raw SQLite constraint text~~ — **dropped by the housekeeping
  round (`housekeeping-round-spec.md` §7), not fixed.** It cannot happen:
  the wizard only runs when there is no installation row, and vessel rows
  only exist after an installation has been set up, so the vessel table is
  always empty when the wizard writes to it. Recorded here so it is not
  raised a third time.
- ~~`MainWindow.cpp` uses `QSignalBlocker` without including
  `<QSignalBlocker>` directly~~ — **resolved by the housekeeping round
  (§6).** The explicit include is in.
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
- The certificate list does not live-refresh its already-rendered rows when
  thresholds are changed on the Settings screen without an intervening
  `reload()` trigger. Accepted as shipped (see step 8's judgment call 6
  above) — resolved as a non-issue by step 9, which turned out not to need
  a cross-screen "settings changed" signal after all: the badge/banner
  recompute independently from their own repository reads rather than
  listening for one.
- Step 8's own new/changed files stayed comfortably under the ~300-line
  guideline (`AppSettingRepository.cpp` ~140 lines, `SettingsPage.cpp` ~170
  lines) — no new size debt from this step.
- `CertificateAlertProvider::attentionByVessel()` runs its full
  N+1-per-vessel sweep twice at every startup (once for the banner check,
  once for `refreshAlertBadges()`), and again on every
  `certificatesChanged()` emission. Accepted at this app's target fleet
  size, same call as the per-vessel N+1 pattern itself; first thing to
  look at if startup or a reload ever feels slow.
- The alert banner is a startup snapshot and does not live-update its own
  rows if the underlying data changes while it's still showing (only the
  sidebar badge stays live within a session). Accepted as shipped — the
  banner already reflected the truth at the moment it was shown; revisit
  only if a long-running session without a restart turns out to be a real
  usage pattern worth the extra complexity.
- Step 9's own new/changed files stayed under the ~300-line guideline
  (`CertificateAlertProvider.cpp` ~90 lines, `AlertBanner.cpp` ~115 lines,
  `MainWindow.cpp` grew but stayed comfortably under 300) — no new size
  debt from this step.
- `modules/certificates/ui/CertificateListWidget.cpp` grew past the
  ~300-line guideline across steps 9–11 (433 → 434 → 566), and was split
  immediately after step 11 — see "`CertificateListActions` refactor"
  above. Now **396 lines**, still over guideline but with no remaining
  seam as clean as the one already pulled out; accepted as shipped.
  Worth a fresh look if step 12 (Attachments) pushes it further.
- Step 10's and step 11's own new/changed files otherwise stayed well
  under the guideline (`EndorsementRepository.cpp` grew by ~15 lines,
  `CertificateEditDialog.cpp` is 227 lines, `CertificateExtensionRepository.cpp`
  is 246 lines, `CertificateExtensionDialog.cpp` is 98 lines,
  `CertificateState.cpp` is 215 lines) — the size debt above is
  concentrated entirely in `CertificateListWidget.cpp`.
- `MACOSX_BUNDLE $<CONFIG:Release>` (see "Producing a Release build"
  above) is not documented as a CMake-supported use of that property the
  way `WIN32_EXECUTABLE` is — it's read at configure time to decide the
  target's bundle layout. Inert on Windows today, so nothing is broken;
  on macOS it will likely be treated as an always-truthy non-empty string,
  making *every* config a bundle rather than gating by Release. Fix when
  someone first builds on a Mac: either an `if(APPLE)` block reading
  `CMAKE_BUILD_TYPE` directly, or simply leaving `MACOSX_BUNDLE` always
  `TRUE` — a bundle is the normal shipping form on macOS regardless of
  config, so that may turn out to be the right answer anyway, not a
  workaround.
- `main()` has no way to point the database at anywhere but the real
  `%APPDATA%` folder — it always calls `Database::defaultPath()`. This is
  why `CLAUDE.md` §10.1's "point any manual run at a throwaway data
  directory" has no mechanism behind it today. A `--data-dir` argument
  would give it one, and would also let a future verification pass check a
  screen without going near the real fleet data. **Still open, now in two
  layers rather than one.** Step 12's `ManagedFileStore::setRootPathOverride()`
  still only redirects where *attachments* go, with no effect on
  `Database::defaultFilePath()` itself. The attachment addendum adds a
  second layer on top: `VesselAttachmentRoots::rootForWriting()` checks
  that same override first, then a *vessel's own configured folder* —
  so a manual run against the real database also attaches real documents
  into whichever folder each real vessel is actually pointed at, unless
  the override happens to be set. All three gaps (database path,
  installation-wide override, per-vessel folder) share the same root
  cause and the same fix: a `--data-dir` argument plumbed through to all
  three. Worth adding next time `main.cpp`/startup is touched for
  another reason.
- **Updated by addendum 2** (counts below are current, verified directly
  with `wc -l`, not carried over from the previous entry). Of the five
  files the first addendum left over the ~300-line guideline,
  `CertificateEditDialog.cpp` is **resolved** — down to 218 lines, since
  `EndorsementSection` was split out of it for addendum 2's own reasons
  and happened to pull it back under the guideline as a side effect —
  and `CertificateListActions.cpp` **improved**, from 320 down to 306,
  for the same reason (`CertificateAttachmentActions` was split out of
  it). `core/VesselRepository.cpp` is **unchanged** at 366 — addendum 2
  did not touch it. Two grew further: `core/ManagedFileStore.cpp` from
  328 to **424 lines** (`deleteAt()`, `entriesIn()`,
  `moveNamedEntries()`, though `FileTree.cpp`/`.h` were split out of it
  along a real seam at the same time, which is what kept the growth to
  this rather than more). `CertificateListWidget.cpp` from 481 to **485
  lines** — essentially flat, and still the single most over-guideline
  file in the project. Two more newly crossed the guideline this
  addendum: `modules/certificates/data/EndorsementRepository.cpp`, from
  comfortably under 300 to **414 lines** (`update()`, `softDelete()`,
  `loadParentOfEndorsement()`), and `core/VesselAttachmentRoots.cpp`,
  new in the first addendum and under the guideline then, now **330
  lines** (the marker file logic, `moveNamedEntries()`-based folder
  change). `modules/certificates/data/CertificateRepository.cpp`, long
  since over the guideline, grew again to **402 lines**
  (`softDeleteWithChildren()`). None split further — Claude Code's own
  call, which I'm accepting: each is one class's whole implementation
  with no seam that wouldn't just spread that one class across two
  translation units, unlike the UI splits above which had a genuine
  conceptual boundary (the document vs. the certificate's lifecycle;
  the certificate's own fields vs. its endorsements). Watching all five;
  `CertificateListWidget.cpp` remains the first candidate to look at if
  this screen is touched again.
- Step 12's new/changed files mostly stayed well under the ~300-line
  guideline (`ManagedFileStore.cpp` 259 lines, `CertificateAttachmentRepository.cpp`
  228 lines, `CertificateEditDialog.cpp` 286 lines). The one exception:
  `CertificateListActions.cpp` grew to 319 lines in the step right after
  it was extracted from `CertificateListWidget.cpp` for being too long —
  accepted as shipped (see step 12's judgment call 3 above), not split
  again for 19 lines.
- The service-layer scope-check-after-file-copy ordering in
  `CertificateAttachmentService::replaceCurrent()` — accepted as a
  documented gap, not fixed; full reasoning under step 12's judgment
  call 2 above. Revisit if a second caller of the service (outside the
  UI) ever appears.
- **Resolved for step 12.** Commit `04ab243`'s leading UTF-8 BOM was
  amended away in place (new hash `fdf2e22`, tree identical, pushed with
  `--force-with-lease` before anything was built on top of it). Step 10's
  `8ba1347` still carries its own BOM and stays that way — nine commits
  back, fixing it needs an interactive rebase of everything after it,
  not worth the risk for a cosmetic character. The real cause was never
  fully root-caused (something in how PowerShell 5.1 feeds a native
  command's stdin, not `Out-File` and not a plain pipe — both were tried
  and both still produced a BOM), but the working fix doesn't depend on
  knowing why: write the message file with
  `[System.IO.File]::WriteAllText($path, $msg, (New-Object
  System.Text.UTF8Encoding $false))`, check the file's own first bytes
  are `53 74 65 70` before handing it to git, then `git commit -F
  $msgFile`. That file-bytes check — not "this approach should be
  fine" — is the thing to trust from now on; two different
  should-be-fine claims (mine, both times) turned out wrong before this
  one was actually verified against the raw bytes.
- **Resolved.** The `InstallationContext`-by-reference dangling bug
  (see "InstallationContext storage fix" above) is closed at the type
  level — every class that stores one now holds it by value, so no
  future call site can reintroduce it, unlike the ~50-call-site
  mechanical alternative that was considered and rejected. Neither this
  fix nor the survey-deadline fix added any new file to the over-300-line
  list above; both touched are small (the changed repository headers are
  all under 85 lines; `CertificateState.cpp` is still ~230 lines).
- `modules/certificates/ui/CertificateListWidget.cpp` grew again in step
  13, from 485 to **510 lines** — the new IO button row and the three
  extra constructor parameters. Still the single most over-guideline
  file in the project, now by a wider margin; accepted as shipped for
  the same reason as before (no seam left as clean as the ones already
  pulled out: `CertificateListActions`, `CertificateIoActions`). The one
  that does look real now: the row-rendering logic (`fillRow()` and the
  column-filling code around it) is a plausible further split, the same
  shape as the actions classes already extracted. Worth a dedicated pass
  before this file grows again, rather than folding it into a future
  step's own changes.
- Step 13's own new files mostly landed well clear of the guideline
  (`CertificateCsvExporter.cpp` 104 lines, `CertificateCsvFileChecks.cpp`
  74, `CertificateCsvValues.cpp` 75, `CertificateIoActions.cpp` 184,
  `CertificateImportPreviewDialog.cpp` 174) — two were split before
  being committed to stay under it (`CertificateCsvFormat.cpp` 286,
  `CertificateCsvImporter.cpp` 297, both noted above). No new size debt
  from this step beyond `CertificateListWidget.cpp`'s continued growth.
- ~~`core/TransactionScope::commit()` clears its own "transaction open"
  flag before calling the underlying `QSqlDatabase::commit()`~~ —
  **resolved by the housekeeping round (§6).** The flag is now cleared
  only once the commit has succeeded, so a failed commit leaves the
  destructor able to roll back.
- **Resolved by the usability fixes round.** `CertificateListActions.cpp`,
  over the guideline since step 12 (320 → 306 → 319 across the steps
  above), is back under it at **224 lines** — `CertificateDeleteActions`
  was split out of it for this round's own reasons (plural confirmation,
  a best-effort loop, a combined summary), not specifically to fix the
  size debt, but it did. `CertificateListWidget.cpp` grew again instead,
  510 → **554 lines**, for the header-settings wiring and the
  `ExtendedSelection` change — still the single most over-guideline file
  in the project, now by the widest margin yet. The row-rendering split
  flagged as a plausible next step after step 13 (`fillRow()` and the
  column-filling code around it) remains the most real seam, and is now
  more worth doing than it was last round, not less. `CertificateCsvFormat.cpp`
  (302) and `CertificateCsvImporter.cpp` (311) were untouched by this
  round's changes to either file's structure — same counts as step 13
  left them, confirmed directly, not re-split.
- **Added by the "latest survey counts" round.** Two test files are over
  the guideline, both already over it before this round touched them:
  `tests/modules/certificates/tst_CertificateStateSurvey.cpp` 316 →
  **426 lines** (three new cases plus the renamed one) and
  `tst_CertificateCsvImportSurveys.cpp` 417 → **429** (one rewritten
  case). Unlike the repository and widget files above, these have an
  obvious seam and no one-class-per-file conflict: the five matching-order
  cases — `theLatestSurveyCoversEveryAnniversaryBeforeIt`, the V TRE
  case, the late-note case, the trade-off case and
  `anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing` — are a
  self-contained group and would make a `tst_CertificateStateSurveyMatching`
  of about 180 lines, leaving the original around 250. Not done in this
  round, which was a behaviour fix; a candidate for the housekeeping round
  that `roadmap-and-pending.md` already schedules before step 15.
  `SurveySchedule.cpp` itself went 178 → 200 lines and is fine.
- **Updated by the housekeeping round — the file-size picture as it now
  stands.** `housekeeping-round-spec.md` applied §9's helper-class rule to
  the two worst offenders, and both came down a long way without being
  split across files:
  - `core/VesselRepository.cpp` **557 → 430** (307 code).
    `VesselImoNumberCheck` and `VesselArchiveWriter` are out, both in
    `core/`, both used only by this repository. Still over, and stopping
    there: what is left is the eight CRUD methods and the row mapping,
    and the only other separable piece — the positional column list and
    `vesselFromRow()` — must not cross a file boundary from the queries
    that read it in step.
  - `modules/certificates/ui/CertificateListWidget.cpp` **554 → 379**
    (244 code), after `CertificateRowRenderer` (`ui/`) and
    `CertificateListRows` (`service/`). Still over; the remaining bulk is
    one 125-line constructor, and a class that only calls `addWidget()`
    on ten things handed to it is not a boundary worth having.
  - `tests/…/tst_CertificateStateSurvey.cpp` **426 → 285**, with
    `tst_CertificateStateSurveyMatching.cpp` (165) taking the five
    matching cases. Resolved.
  - `tests/…/tst_CertificateCsvImportSurveys.cpp` **430, unchanged and
    left alone** — nine cases over one 66-line fixture, so every cut
    either duplicates the fixture or needs a new shared support header.
    Reported rather than forced, per that spec's §1 and §4.
  - `core/VesselAttachmentRoots.cpp` **330 → 342** and
    `app/MainWindow.cpp` **322 → 323**: both were already over before
    this round, and each grew by the least that would do the job
    (`configuredRoot()`, and one include). Not split — the round's brief
    named specific files, and the only remaining candidate in the project
    with an obvious seam is the certificate list's constructor.
  - Untouched and still over, unchanged since earlier rounds:
    `CertificateRepository.cpp` (~310),
    `tst_CertificateRepositoryValidation.cpp` (~301), `ManagedFileStore`
    (424), `EndorsementRepository` (414).
- **Noted by the housekeeping round, not acted on:**
  `CertificateAlertProvider` and the new `CertificateListRows` gather the
  same thing — each certificate's endorsements, its active extension, the
  alert thresholds, then `computeCertificateState()` — one over the fleet
  and one over a single vessel, and comments in both say the two have to
  agree or the badge and the list will disagree about the same
  certificate. Now that the list's half is a service class, having the
  provider use it is the obvious next move. Left for its own round: it
  changes what the badge counts if the two loops turn out to differ
  anywhere, which is not a housekeeping change.

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

**Step 15 — database backup & restore.** The last round of Certificate
Control, and the module is complete after it. The order is in
`docs/roadmap-and-pending.md`; `CLAUDE.md` §11 matches it.

Export Database / Import Database in Settings — a safe, in-app way to
move to another computer or keep a copy somewhere. No spec yet; bring it
up here first, the same way every step since step 3 has gone. The audit
trail viewer is deferred, not part of this module.

The housekeeping round above closed out the two small carried fixes
(`TransactionScope`, `<QSignalBlocker>`), dropped the first-run
duplicate-IMO item as impossible, and brought the two worst files down to
430 and 379 lines. What file-size debt remains is listed above with the
reason for each; the one piece of real work it deliberately left behind is
making `CertificateAlertProvider` share `CertificateListRows`, which is a
change to what the badge counts and so wants its own round rather than a
housekeeping one.

Step 14 (Reports) was **dropped by the developer**: step 13's one-vessel
PDF statement is enough for this module. A fleet-wide statement, a print
preview or per-certificate history become additional scope on
`CertificateReportBuilder` if and when a concrete need shows up.

The endorsement-model question carried here since step 13 — whether to
keep only the most recent survey per track — was **settled** by the
"latest survey counts" round: keep the full history in the `endorsement`
table, and schedule forward from the latest survey. The schedule only
ever reads the latest survey, but the history is itself the record the
Endorsements list, the PDF statement and the audit trail show.

Full build order is in `CLAUDE.md` §11.
