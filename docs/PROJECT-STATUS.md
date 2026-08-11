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

---

## Decisions confirmed by the developer

- `schema_version` is **exempt** from the `origin_node` and `revision` columns
  required by `CLAUDE.md` §6.5. It is local bookkeeping and never replicates.
  All other tables carry the full set.
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

## Outstanding technical debt

- `MigrationRunner.cpp` is ~283 lines, near the ~300 limit in `CLAUDE.md` §9.
  **Agreed action: extract the statement splitter into its own file
  (`core/SqlStatementSplitter`) before the next feature touches it.**
- No SQLite viewer installed on the machine. DB Browser for SQLite is the
  suggested tool — a viewer the developer runs, not a project dependency.
- Qt Creator breakpoints not working (see Environment above).

---

## Next step

**Step 3 — First-run wizard.** On first launch, ask whether this is an OFFICE or
a VESSEL installation; if VESSEL, capture the vessel identity. Write the single
row into the `installation` table that migration 001 created. Expose it through
an `InstallationContext` that the repository layer will later consult for the
vessel filter (`CLAUDE.md` §3).

Then step 4: Vessel CRUD — the first complete vertical slice from database to
screen.

Full build order is in `CLAUDE.md` §11.
