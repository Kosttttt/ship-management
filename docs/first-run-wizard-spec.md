# First-Run Wizard & Vessel Identity — Specification

Version 1.0. This document defines *what step 3 does*. Architecture rules
live in `CLAUDE.md` at the repository root; this is core infrastructure, not
a module, so it is owned the same way `installation` is (`CLAUDE.md` §5).

---

## 1. Purpose

Every installation of the app must know, from the very first launch, whether
it is running at the office (whole fleet) or aboard a specific vessel (that
vessel only). This is recorded once, permanently, and everything downstream —
the vessel filter (`CLAUDE.md` §3), the sync package, the window title —
depends on it being set correctly before any other data exists.

This step also introduces the `vessel` table (migration 002), moved earlier
than originally planned: the wizard cannot write a VESSEL installation row
without a vessel row for it to point at, so the table that used to be
step 4's job is created now. Step 4 (Vessel CRUD) still owns editing it.

## 2. Scope

In scope:

- Migration 002 — the `vessel` table.
- A first-run wizard (`QWizard`) that runs once, before `MainWindow` exists,
  when the `installation` table is empty.
- `core::InstallationContext` — loaded at startup, exposes the mode for the
  rest of the app to read. Read-only after startup; installation mode is
  permanent (§7).
- The permanent mode indicator in the main window title bar.

Out of scope (step 4 and later):

- Editing vessel data after the wizard (call sign, gross tonnage, port of
  registry, flag state) — the wizard does not collect these.
- Any repository consulting `InstallationContext::vesselScope()` — that
  starts with the Vessel repository in step 4.
- Changing installation mode after first run (§7) — an Administrator action
  deferred to step 15.

## 3. The IMO check digit

IMO numbers are validated by check digit **at every entry point** (this
wizard, and later the Vessel CRUD form), and stored as `TEXT`.

An IMO number is seven digits, `d1 d2 d3 d4 d5 d6 d7`, where `d7` is the
check digit. It is valid when:

```
(7*d1 + 6*d2 + 5*d3 + 4*d4 + 3*d5 + 2*d6) mod 10 == d7
```

Worked example — `9074729`:

```
7*9 + 6*0 + 5*7 + 4*4 + 3*7 + 2*2
= 63 + 0 + 35 + 16 + 21 + 4
= 139  →  last digit 9  →  matches d7 = 9  →  valid
```

Rejected as invalid input: fewer or more than 7 characters, any non-digit
character, or a correct-length numeric string whose check digit doesn't
match. Whether the user types "IMO 9074729" or "9074729", strip any
non-digit characters before validating — the field should tolerate the
"IMO" prefix people are used to writing without requiring them to remove it.

This is a pure function with no Qt widgets and no SQL, so it lives in the
domain sense of `CLAUDE.md` §4.1 even though it sits under `core/` rather
than a module's `domain/` folder — `core/` already owns `Vessel` and
`InstallationContext` per the folder layout in `CLAUDE.md` §8, and this
validator is used by both the wizard (core) and, later, the Vessel CRUD form
(a module-adjacent screen), so it belongs where both can reach it without
either depending on the other.

Proposed location: `core/ImoNumberValidator` — a single free function,
directly unit-testable with no database and no window.

## 4. The wizard

Built with `QWizard`, two pages, run modally before `MainWindow` is
constructed.

**Page 1 — Installation mode.** A radio choice, OFFICE or VESSEL, each with
one line explaining what it means ("OFFICE — this computer manages the whole
fleet" / "VESSEL — this computer belongs to one ship"). Selecting VESSEL
enables page 2; selecting OFFICE skips straight to Finish
(`QWizardPage::nextId()`).

**Page 2 — Vessel identity** (VESSEL only). Two fields:

- **Vessel name** — required, free text.
- **IMO number** — required, validated live against §3 as the user types.
  Finish is disabled while the field is invalid or empty, with an inline
  error shown under the field.

Nothing else is asked here. Call sign, gross tonnage, port of registry, and
flag state are left for step 4.

**On Finish**, in a single database transaction (`CLAUDE.md` §6 — nothing
about this write is allowed to half-succeed):

1. Generate `installation.id` (UUID via `QUuid::createUuid()`).
2. Build `node_id`: `'OFFICE'`, or `'VESSEL-' + imoNumber` for VESSEL.
3. If VESSEL: generate `vessel.id` (UUID), insert the `vessel` row first,
   then insert the `installation` row with `vessel_id` pointing at it.
4. If OFFICE: insert the `installation` row with `vessel_id = NULL`.
5. `created_by` / `updated_by` on both rows: `'SYSTEM'`, consistent with the
   precedent already set for migration records — no user system exists yet.
6. Commit. On any failure, roll back both inserts and show the wizard's
   error state again rather than leaving a partial installation row.

If the user closes the wizard without finishing, the app exits. It cannot
proceed without a recorded installation mode, and nothing else is allowed to
write until one exists.

## 5. Data model (indicative)

`vessel` carries the standard audit and sync columns from `CLAUDE.md` §6.5.

```
vessel
    id, name, imoNumber, callSign, grossTonnage, portOfRegistry, flagState
```

- `imoNumber` — `NOT NULL`, `UNIQUE`. Two vessels sharing an IMO number is
  a data error, and this matters beyond the wizard: an OFFICE installation
  will eventually hold every vessel in the fleet (entered directly, or
  arriving via sync from each ship), so the constraint is doing real work
  from step 4 onward even though only one vessel row is ever created by the
  wizard itself.
- `callSign`, `grossTonnage`, `portOfRegistry`, `flagState` — nullable. Not
  collected here; filled in via the Vessel CRUD edit form in step 4.
- Field names follow the IMO Compendium naming already fixed in
  `CLAUDE.md` §9.

No change is needed to the `installation` table itself (migration 001) —
`vessel_id` already exists as a plain `TEXT` column.

**Correction (post-implementation):** migration 001 never declared
`REFERENCES vessel(id)` on that column, so there is no enforced foreign key
here — `PRAGMA foreign_keys = ON` has nothing to check. That earlier
paragraph conflated "the referenced table exists in time" with "a foreign
key constraint exists"; only the first is true. The safety this section
actually relies on is the single transaction in §4 step 6: the `vessel` row
and the `installation` row are written together or not at all, so an
orphaned `installation.vessel_id` cannot occur through this wizard. Real
referential integrity would need a rebuilt `installation` table (SQLite
can't `ALTER TABLE ADD CONSTRAINT` after the fact) — not worth doing for a
single-writer local file where the transaction already prevents the bad
state. Revisit only if a future change starts writing `installation` or
`vessel` from more than one place without going through
`InstallationRepository`.

## 6. Startup gating

On every launch, after `core::Database` connects and `MigrationRunner`
applies pending migrations:

1. Query `installation` for a row.
2. **Zero rows** → run the wizard modally. `MainWindow` is not constructed
   until the wizard finishes successfully.
3. **One row** → skip the wizard, load the row into `InstallationContext`,
   proceed straight to `MainWindow`.

Recovery path for a wrong first-run choice (already agreed, no change UI, no
password — see §7): delete the database file and relaunch. This is only
safe because of the ordering above — the wizard is the first thing capable
of writing, so an empty `installation` table implies nothing else has data
either, at true first run. It stops being a clean recovery the moment real
data exists alongside it, which is expected and accepted, not a defect.

## 7. Installation mode is permanent

No UI to change it later, and deliberately no password gate — a password
with no user system behind it is just a constant sitting in the source code,
which is a false sense of security, not real access control. Changing mode
becomes an Administrator action at step 15, once roles exist to gate it
properly.

## 8. `core::InstallationContext`

Populated once at startup — either freshly from the wizard's write, or by
reading the existing `installation` row — and treated as read-only for the
rest of the process lifetime, matching §7.

Indicative interface:

```cpp
enum class InstallationMode { Office, Vessel };

class InstallationContext {
public:
    InstallationMode mode() const;
    QString          nodeId() const;
    QString          vesselId() const;   // empty when Office
    QString          vesselScope() const; // CLAUDE.md §3 — what repositories consult
};
```

Step 3 only needs to build and populate this. Nothing consults
`vesselScope()` yet — that starts with the Vessel repository in step 4.

## 9. UI — permanent mode indicator

`MainWindow`'s title, set once at construction from `InstallationContext`
and never changed afterward (mode is permanent):

- OFFICE: `"Ship Management System — OFFICE"`
- VESSEL: `"Ship Management System — VESSEL: <name> (<imoNumber>)"`

## 10. Edge cases that must have unit tests

1. IMO check digit: known valid number (`9074729`), and each of — wrong
   length, non-digit characters, correct length with a wrong check digit.
2. Wizard cancelled before Finish → app exits, `installation` table still
   empty, no orphaned `vessel` row.
3. A simulated failure between the `vessel` insert and the `installation`
   insert → transaction rolls back, neither row exists.
4. Relaunch with an existing `installation` row → wizard does not appear,
   `InstallationContext` and the title bar reflect the stored row correctly
   for both OFFICE and VESSEL.
5. `imoNumber` `UNIQUE` constraint is enforced at the database level (insert
   a second vessel with a duplicate IMO number and expect rejection) — this
   is really a step-4 test, since the wizard itself only ever inserts one
   vessel row, but the constraint is introduced by this migration.
