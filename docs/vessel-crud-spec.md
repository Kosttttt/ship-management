# Vessel CRUD — Specification

Version 1.0. This document defines *what step 4 does*. Architecture rules
live in `CLAUDE.md`; the `vessel` table itself was created early, by
migration 002, and is described in `docs/first-run-wizard-spec.md` §5. This
step does not touch the schema — it builds the first screens and the first
`UPDATE` statements the project has written.

---

## 1. Purpose

Give each installation a working view of the vessel data the wizard already
started. An OFFICE installation needs to see and maintain every vessel in
the fleet. A VESSEL installation has exactly one vessel — itself — and needs
to fill in the identity fields the wizard didn't ask for (call sign, gross
tonnage, port of registry, flag state) and correct any of them, including
the IMO number, if a mistake was made at entry.

This is also the first complete vertical slice: database → repository →
screen, for a real everyday action, not a once-ever wizard.

## 2. Scope

In scope: list vessels (OFFICE only), create a vessel (OFFICE only), edit
any vessel field including the IMO number, `InstallationContext::vesselScope()`
enforced inside the repository for the first time.

Out of scope, deliberately deferred: removing a vessel from the fleet
(soft-delete, `CLAUDE.md` §6.4). Nothing yet references a vessel by foreign
key — the certificates module doesn't exist — so there's no real rule to
design a "remove" action against yet (does it check for open certificates?
pending work orders? both don't exist). It returns as a small follow-up once
a module that actually depends on vessels exists, so the rule is designed
against a real need instead of a guess. Also out of scope: navigation chrome
(sidebar, `ModuleRegistry`) — there is only one screen to show right now, so
`MainWindow`'s central widget *is* that screen; the shell described in
`CLAUDE.md` §8 waits for a second screen to navigate between.

## 3. Where this lives

`Vessel` is core-owned (`CLAUDE.md` §5, §8), so it follows the same
placement `InstallationContext`/`InstallationRepository` set in step 3:

- `core/Vessel.h` — a plain struct, no behaviour.
- `core/VesselRepository.h/.cpp` — the only file with SQL for this table.
- No service layer. There is no business rule yet beyond field validation,
  which the domain-layer `ImoNumberValidator` and the repository's own input
  checks already cover (`CLAUDE.md` §4 rule 3 — a widget may format and
  display, not decide). Inserting a service layer later, once a real
  cross-cutting rule appears (a "remove" action that has to check for open
  certificates, say), is a cheap addition — not worth building ahead of a
  reason (`CLAUDE.md` §10).

```cpp
struct Vessel {
    QString id;
    QString name;
    QString imoNumber;
    QString callSign;
    int     grossTonnage = 0;   // 0 means "not entered", never a float (CLAUDE.md §6.8)
    QString portOfRegistry;
    QString flagState;
};
```

## 4. Screens

One reusable form, two different wrappers around it — this avoids defining
the same six fields twice.

**`app/VesselEditForm`** — a plain `QWidget`, not a dialog. Holds the six
input fields. The IMO field re-uses the exact live-validation pattern
`VesselIdentityPage` already established in step 3: `ImoNumberValidator`
runs on every keystroke, an inline label reports `Empty` / `WrongLength` /
`CheckDigitMismatch`, and a `formValid()`-style signal tracks whether the
form may be saved. Uniqueness is **not** checked live — it needs a database
query, and checking on every keystroke would mean a query per character
typed. It is checked once, on Save (§6).

**`app/VesselEditDialog`** (OFFICE only) — wraps `VesselEditForm` in a
`QDialog` with OK/Cancel. Used for both Add (empty form, calls
`VesselRepository::create()`) and Edit (form pre-filled, calls
`VesselRepository::update()`). Opened from `VesselListWidget`.

**`app/VesselListWidget`** (OFFICE only) — a table (Name, IMO Number, Flag
State, Call Sign), sortable by clicking a column header. No search box in
this version — the fleets this app manages are small enough that scrolling
a sorted list is enough for now; add filtering later if real use shows a
need. An "Add Vessel" toolbar button and double-click-to-edit both open
`VesselEditDialog`; the list refreshes from `VesselRepository::list()` after
the dialog is accepted.

**`app/VesselDetailPage`** (VESSEL only) — embeds `VesselEditForm` directly,
no dialog chrome, because there is no list to return to. Loads the one
vessel this installation owns via `InstallationContext::vesselId()`. A Save
button commits through `VesselRepository::update()`. A "Discard changes"
action reloads the form from the database rather than closing anything —
there is nothing to close; this screen is always on screen for a VESSEL
installation.

## 5. Validation rules

- `name` — required, trimmed, non-empty. Same rule the wizard already
  enforces.
- `imoNumber` — required, valid check digit (`core/ImoNumberValidator`),
  unique across all vessels **excluding the row being edited**. Editable
  after creation, on purpose (§6) — the real-world IMO number is permanent
  for a hull's life, but a typo made at entry needs a way to be fixed, and
  this screen is that way.
- `callSign`, `portOfRegistry`, `flagState` — optional free text. No
  dropdown or fixed list yet; add one later if data quality becomes a
  problem in practice.
- `grossTonnage` — optional, a non-negative integer. Stored as `INTEGER`
  already (migration 002); the form simply refuses a negative number or
  anything that isn't a whole number.

## 6. Repository behaviour

`VesselRepository` is constructed with a reference to the database
connection and to `InstallationContext`, the same pattern `InstallationRepository`
established.

**The scope filter lives here, not in the screen** (`CLAUDE.md` §3: "the
vessel filter is applied in the Repository layer, on every query, in one
place... never left to a screen to remember"). Only the OFFICE screen calls
`list()` today, but the repository doesn't get to assume that stays true —
so `list()` and `findById()` both apply the filter themselves:

```
if InstallationContext::mode() == Vessel:
    WHERE vessel.id = InstallationContext::vesselScope()
else:
    (no filter — whole fleet)

  ...and always: AND is_deleted = 0
```

The `is_deleted = 0` filter is included from day one even though nothing
can set it yet (§2) — the column already exists per the standard audit set,
and a query that forgets it today is a silent bug waiting for the day
deactivation is added later.

**Defensive checks, matching the precedent step 3 set** (the repository
re-validates rather than trusting the caller, because it is reachable from
tests and from other code, not only from the one screen that currently
calls it):

- `create()` refuses outright under a VESSEL-mode context. Only OFFICE
  creates vessels.
- `update()` refuses to touch any id other than `vesselScope()` when running
  under VESSEL mode — even though the UI never offers another vessel's row
  to a VESSEL installation.
- Both `create()` and `update()` check IMO uniqueness with a direct query
  (`SELECT 1 FROM vessel WHERE imo_number = ? AND id != ? AND is_deleted = 0`)
  **before** attempting the write, and return a friendly message ("This IMO
  number already belongs to another vessel") rather than surfacing the raw
  SQLite constraint text. The database's own `UNIQUE` constraint stays in
  place as the backstop against a race, but it is not the primary path a
  user sees an error through. *(Step 3's wizard didn't do this — it let a
  duplicate IMO fail with the raw SQLite message. Worth backporting there
  too, once step 4 lands, so both places behave the same way; noted, not
  blocking.)*

**Revision handling — new territory.** Step 3 only ever inserted rows once;
this is the first code that updates an existing row. `CLAUDE.md` §6.5's
`revision` column exists for exactly this: `update()` sets `updated_at` /
`updated_by` to the current values and increments `revision` by one.
`created_at` / `created_by` are never touched by an update. `created_by` /
`updated_by` stay `'SYSTEM'` for now, consistent with the precedent already
set — there is still no real user identity to attribute an edit to; this is
the first time a human is directly the cause of a write, but nothing
changes about who gets blamed for it until role-based access exists.

## 7. Navigation

`MainWindow` asks `InstallationContext::mode()` once, at construction, and
sets its central widget directly: `VesselListWidget` for OFFICE,
`VesselDetailPage` for VESSEL. No sidebar, no `ModuleRegistry` yet — those
exist to navigate *between* screens, and there is only one.

## 8. Edge cases that must have unit tests

1. `list()` in OFFICE mode returns every vessel; in VESSEL mode returns only
   the one matching `vesselScope()`, even if a test seeds more than one row
   directly (simulating a future sync bringing in data it shouldn't expose).
2. `list()` and `findById()` exclude `is_deleted = 1` rows.
3. `create()` rejected outright when the context is VESSEL mode.
4. `create()` rejects an empty name, an invalid IMO check digit, and a
   duplicate IMO number, each with no row written.
5. `update()` rejected when the context is VESSEL mode and the target id is
   not `vesselScope()`.
6. `update()` succeeds when re-saving a vessel with its own unchanged IMO
   number — the uniqueness check must exclude the row's own id, or every
   edit of an untouched IMO field would falsely fail.
7. `update()` rejects changing the IMO number to one already used by a
   different vessel.
8. `update()` increments `revision` by exactly one and refreshes
   `updated_at`/`updated_by`, while `created_at`/`created_by` are unchanged
   from the original insert.
9. Editing `grossTonnage` to a negative number, or to a non-integer, is
   rejected by the form before it ever reaches the repository.
