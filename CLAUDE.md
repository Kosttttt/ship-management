# Ship Management System — Project Rules

Read this file fully before making any change.
Module behaviour is specified in `docs/certificate-control-spec.md`. This file
holds architecture and conventions; that file holds the rules of the domain.

## 1. What this project is

A modular desktop application for ship management, used by vessel crew at sea
and by shore-based office staff. It runs offline on the vessel and synchronises
with the office when a connection is available.

Modules are built **one at a time**. Each module must be fully usable on its own
before the next one begins. The current module is stated in section 11.

## 2. Stack — do not substitute without asking

| Concern      | Technology                                          |
|--------------|-----------------------------------------------------|
| Language     | C++17                                               |
| GUI          | Qt 6 Widgets (NOT QML, NOT Qt Quick)                |
| Build        | CMake (>= 3.21)                                     |
| Compiler     | MinGW/GCC on Windows, GCC on Linux, Clang on macOS  |
| Database     | SQLite via Qt SQL (QSQLITE driver), office and vessel alike |
| Tests        | Qt Test                                             |
| Reporting    | QTextDocument + QPrinter (print + PDF)              |
| Tabular I/O  | CSV first. `.xlsx` via QXlsx only if CSV proves insufficient |

Cross-platform (Windows, macOS, Linux) from a single codebase is a hard
requirement. Never use platform-specific APIs (Win32, `windows.h`, registry,
backslash path separators). Use Qt equivalents: `QDir`, `QStandardPaths`,
`QFile`, `QDateTime`.

Do not add a third-party dependency without asking first.

## 3. Two installation modes, ONE binary

The same executable runs as either an office installation or a vessel
installation. There are never two codebases and never two builds.

On first launch the user chooses the mode. It is stored in a single-row
`installation` table in the core schema:

```sql
CREATE TABLE installation (
    id                 TEXT PRIMARY KEY,
    installation_mode  TEXT NOT NULL,   -- 'OFFICE' | 'VESSEL'
    node_id            TEXT NOT NULL,   -- 'OFFICE' | 'VESSEL-<IMO>'
    vessel_id          TEXT,            -- UUID, NULL when OFFICE
    -- ...audit columns...
);
```

| Behaviour                | OFFICE            | VESSEL                  |
|--------------------------|-------------------|-------------------------|
| Vessels visible          | entire fleet      | its own vessel only     |
| Ship selector in toolbar | shown             | hidden                  |
| Dashboard counts         | whole fleet       | its own vessel only     |
| Sync package             | one per vessel    | one, for itself         |

**The vessel filter is applied in the Repository layer, on every query, in one
place.** It is never applied in the UI and never left to a screen to remember.
A screen that forgets would leak another vessel's data to a crew member.

Expose it as a single accessor (e.g. `InstallationContext::vesselScope()`) that
repositories consult. Add a unit test asserting that in VESSEL mode no
repository can return a row belonging to another vessel.

## 4. Architecture — dependencies point downward only

```
UI layer          Qt widgets, dialogs, table models   — knows screens
Service layer     business workflows                  — knows rules
Domain layer      plain C++ entities and logic        — knows nothing else
Repository layer  SQL, database access                — knows storage
Core / infra      Database, Migrations, Logger, AuditTrail, Config,
                  InstallationContext, Vessel registry
```

Rules, in order of importance:

1. **The domain layer contains no Qt widgets and no SQL.** Plain C++ structs,
   enums and free functions. `QDate` / `QDateTime` / `QString` are permitted;
   hand-rolling date arithmetic would be worse. No widgets, no database, no I/O.
2. **SQL appears only in the Repository layer.** A `SELECT` or `INSERT` in a
   widget or a service is a bug.
3. **The UI layer contains no business rules.** A widget may format and display
   a value; it may not decide what the value should be.
4. Nothing ever calls upward.

**Why:** business rules must be reusable by the UI, the report generator, the
alert engine and the sync engine, and unit-testable without a window or a
database.

## 5. Which layer owns which table

- **Core owns things that exist independently of any module:** `installation`,
  `vessel`, `audit_log`, `app_setting`, `schema_version`.
- **The certificates module owns:** `certificate_type`, `certificate`,
  `endorsement`, `extension`, `attachment`.

There is **no equipment registry in this project**. Equipment and planned
maintenance belong to a future PMS module and are not this module's concern.
A certificate for an item of equipment is entered as an ordinary certificate
against the vessel, with a free-text `applies_to` field describing the item.

A module must never `#include` a header from another module's `domain/`,
`data/` or `ui/` folder. Cross-module communication goes through core only.

## 6. Non-negotiable data rules

These exist because two databases (ship and office) both create records while
offline. They cannot be retrofitted later.

1. **Primary keys are UUIDs stored as TEXT.** Never
   `INTEGER PRIMARY KEY AUTOINCREMENT`. Generate with `QUuid::createUuid()`.
   Reason: integer IDs collide when two offline databases merge.
2. **All timestamps are UTC, stored as ISO-8601 TEXT.** Convert to local time
   only for display. Reason: a vessel changes time zone weekly.
3. **Calendar dates** (issue, expiry, endorsement, survey) are stored as
   `YYYY-MM-DD` TEXT and are timezone-free. A certificate expiry is a calendar
   date, not an instant.
4. **Soft delete only.** Set `is_deleted = 1`. Never `DELETE FROM`. Reason: a
   deletion must replicate to the other side, and audit trails must survive.
5. **Every table carries these columns:**

```sql
id           TEXT PRIMARY KEY,      -- UUID
created_at   TEXT NOT NULL,         -- ISO-8601 UTC
created_by   TEXT NOT NULL,
updated_at   TEXT NOT NULL,         -- ISO-8601 UTC
updated_by   TEXT NOT NULL,
is_deleted   INTEGER NOT NULL DEFAULT 0,
origin_node  TEXT NOT NULL,         -- 'OFFICE' or 'VESSEL-<IMO>'
revision     INTEGER NOT NULL DEFAULT 1
```

6. **Schema changes only ever happen via a new migration file.** Never edit a
   committed migration. Reason: installed copies already ran it.
7. **Derived values are never stored.** Certificate status, days remaining and
   survey windows are computed from dates at the moment of display, using
   today's date. Reason: a stored status goes stale the moment the app is
   closed for a week, and a stale alert is worse than no alert.
8. **Money and quantities are never `float` or `double`.** Use integer minor
   units.
9. **File paths stored in the database are always relative** to the managed
   data folder, never absolute. Reason: the folder is copied between ship and
   office, and between machines.

## 7. Module pattern

Each module implements `IModule` and is registered at compile time in
`ModuleRegistry`. There is no dynamic plugin loading.

```cpp
class IModule {
public:
    virtual ~IModule() = default;
    virtual QString id() const = 0;                      // "certificates"
    virtual QString displayName() const = 0;
    virtual QIcon   icon() const = 0;
    virtual QWidget* createMainWidget(QWidget* parent) = 0;
    virtual QList<Migration> migrations() const = 0;
    virtual QList<AlertProvider*> alertProviders() = 0;  // feeds sidebar badge
};
```

Adding a module must require zero changes to existing modules.

## 8. Folder layout

```
src/
  main.cpp
  app/                    shell: MainWindow, ModuleRegistry, navigation, badges
  core/                   Database, MigrationRunner, Logger, AuditTrail,
                          Config, InstallationContext, Vessel
  modules/
    certificates/
      domain/             entities + rules; no widgets, no SQL
      data/               repositories; SQL lives here
      service/            workflows
      ui/                 widgets, dialogs, table models
migrations/               NNN_description.sql, applied in numeric order
docs/                     specifications, including the module spec
tests/                    unit tests, mirroring src/
resources/                icons, seed data, report templates
```

## 9. Coding conventions

- Classes `PascalCase`; functions and variables `camelCase`; members `m_`;
  constants `kPascalCase`.
- One class per file; filename matches the class name.
- `#pragma once` for header guards.
- Prefer `const` and references; pass heavy objects as `const T&`.
- No raw `new`/`delete` outside Qt parent-child ownership; otherwise
  `std::unique_ptr` / `std::make_unique`.
- No `using namespace` in headers.
- `enum class`, never plain `enum`.
- Functions do one thing; split anything over ~40 lines.
- **No source file exceeds ~300 lines.** Split along layer boundaries.
- Vessel field naming follows the IMO Compendium data model: `imoNumber`,
  `callSign`, `grossTonnage`, `portOfRegistry`, `flagState`.

## 10. How to work with the user

The user is a **complete beginner in C++** with basic programming logic, and is
an experienced maritime professional. Defer to them on domain rules; explain
carefully on C++.

- Build **one step at a time**. Finish and verify a step before starting the
  next. Do not scaffold five files ahead.
- After writing code, **explain what it does in plain language**, including why
  each design choice was made.
- Prefer clear code over clever code. No template metaprogramming, no premature
  abstraction, no design pattern without a stated reason.
- When there is a trade-off, state both options and recommend one with
  reasoning — never silently pick.
- Suggest a git commit after each working step.
- Never change the rules in this file without asking first.

## 11. Current status

- **Active module:** Certificate Control — see `docs/certificate-control-spec.md`
- **Build order:**
  1. Toolchain verified (blank Qt window builds and runs)
  2. Core infra: database connection, migration runner, logger
  3. First-run wizard: installation mode + vessel identity
  4. Vessel CRUD — first complete vertical slice
  5. Certificate type catalogue + seed data
  6. Certificate CRUD
  7. Endorsement model and `computeCertificateState()` + unit tests
  8. Certificate list screen: status colours, filters, days-left column
  9. Alerts: sidebar badge, daily toast, filtered drill-down
  10. Renewal workflow
  11. Attachments with archive-on-replace
  12. CSV import/export
  13. Reports (print preview, PDF)
  14. Audit trail viewer + backup/restore
- **Not yet started:** ship-to-shore sync, role-based access control
  (deliberately last), all future modules (DMS, PMS, spares, purchasing, vessel
  life monitoring, budget, crew).

Role-based access control is applied **on top of** working functionality at the
end. Do not design around roles now — but record `created_by` / `updated_by` on
every row so the audit trail is already in place when roles arrive. Note that
"crew see only their own ship" is already delivered by VESSEL installation mode
(section 3) and is not a role concern.
