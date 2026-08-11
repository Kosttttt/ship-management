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

---

## Next step

**Step 4 — Vessel CRUD.** The first complete vertical slice from database to
screen: a list of vessels, and add/edit for the four fields the wizard didn't
collect (call sign, gross tonnage, port of registry, flag state), reusing
`core/ImoNumberValidator` for the IMO field. This is also where
`InstallationContext::vesselScope()` gets its first consumer — the Vessel
repository applies the VESSEL-mode filter from `CLAUDE.md` §3 for the first
time.

A dedicated spec doc for step 4 hasn't been written yet — bring it up here
before handing anything to Claude Code, the same way step 3 went.

Full build order is in `CLAUDE.md` §11.
