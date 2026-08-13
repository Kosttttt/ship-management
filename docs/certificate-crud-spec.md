# Certificate CRUD — Specification

Version 1.0. This document defines *what step 5 does*. Domain rules for
certificates live in `docs/certificate-control-spec.md`; architecture rules
live in `CLAUDE.md`. This step also builds the navigation shell that's been
deferred since step 4, because certificates are the first thing that
actually needs it.

---

## 1. Purpose

Add, view, and edit certificates against a vessel. Nothing about surveys,
endorsements, or computed status yet — this step is the data itself: a
certificate exists, belongs to one vessel, and carries the fields a human
enters when adding it (`certificate-control-spec.md` §5, without
`certificate_type` — see that section for why).

## 2. Scope

In scope: the `certificate` table, a repository, an add/edit form, and a
list screen — reachable from both OFFICE (any vessel in the fleet, chosen
from a toolbar selector) and VESSEL (its own vessel only, no selector).
Also in scope, as a necessary enabler: `IModule`/`ModuleRegistry`
(`CLAUDE.md` §7, specified but never built) and a sidebar in `MainWindow`,
since this is the first point where there's a second thing to navigate to.

Out of scope, deferred to later steps per `CLAUDE.md` §11: endorsements and
`computeCertificateState()` (step 6); status colours, filters, and the
days-left column on the list (step 7 — they need step 6's engine); the
sidebar alert badge (step 8, once `IModule::alertProviders()` has something
to report); the renewal workflow and `previous_certificate_id` (step 9);
attachments (step 11); CSV import/export (step 11); reports (step 12).

## 3. Navigation

**`app/IModule`** — implemented exactly as already specified in `CLAUDE.md`
§7: `id()`, `displayName()`, `icon()`, `createMainWidget()`,
`migrations()`, `alertProviders()`. `CertificatesModule` (at
`modules/certificates/CertificatesModule.h/.cpp`, above the module's four
subfolders) is the first and only implementation. `icon()` can return a
blank/placeholder `QIcon` for now — there are no icon assets in `resources/`
yet, and that's a cosmetic gap, not a blocking one. `alertProviders()`
returns an empty list until step 8 gives it something to report.

**`migrations()` returns an empty list, deliberately not doing real work
yet.** The interface implies each module could contribute its own migration
files for the runner to discover per-module. Restructuring `MigrationRunner`
to do that isn't worth it for one module — `migrations/` stays one flat,
globally-numbered folder exactly as it's always been, and the new file
(`003_create_certificate.sql`) gets added to the CMake list the same way
001 and 002 were. Revisit only if keeping that list in sync becomes a real
problem, which won't happen with a module count this small.

**`app/ModuleRegistry`** — holds a fixed, compile-time list of `IModule`
instances. No dynamic loading (`CLAUDE.md` §7 already rules that out).

**The sidebar.** `MainWindow` gains a simple list on the left: "Vessels"
first (fixed, not a module — it's core, per `CLAUDE.md` §5, and always
present regardless of which feature modules exist), then one row per
registered module. Clicking a row swaps the central widget. This is the
first real exercise of "adding a module must require zero changes to
existing modules" (`CLAUDE.md` §7) — worth watching for whether that
promise actually holds once `CertificatesModule` is wired in.

**The vessel selector.** A toolbar combo box, OFFICE mode only
(`CLAUDE.md` §3's table specified this and it's never been built either),
populated from `VesselRepository::list()`. It starts with nothing selected
— opening Certificates before choosing a vessel shows an explicit "Select a
vessel above to see its certificates" state, not an empty table (confirmed:
no default-to-first-vessel). "Vessels" itself ignores the selector — that
screen has been fleet-wide since step 4 and stays that way. In VESSEL mode
the selector doesn't exist at all; `CertificateListWidget` is constructed
once with `InstallationContext::vesselId()` and never needs to be told
again, the same certainty `VesselDetailPage` already relies on.

For now, `MainWindow` wires the selector's change signal directly to a
`setVesselId()` call on the certificates screen — no general "vessel-scoped
module" abstraction yet. That generalization is worth building the moment a
*second* per-vessel module shows up; with exactly one, it would be solving a
problem that doesn't exist yet.

## 4. Data model

```
certificate
    id, vessel_id
    name                          -- free text, typed by the human
    category                      -- STATUTORY | CLASS | EQUIPMENT | OTHER,
                                      a fixed 4-option picker — a broad bucket
                                      for filtering, not authoritative data
    certificate_number, applies_to    -- both optional
    issue_date                    -- required
    expiry_date                   -- nullable: null means "does not expire"
    issued_by, place_of_issue     -- both optional
    is_interim                    -- checkbox; a real, current-state fact a
                                      human enters at data entry, unlike
                                      previous_certificate_id below
    requires_annual_survey, requires_intermediate_survey
    intermediate_mode             -- ADDITIONAL | REPLACES_ANNUAL, only
                                      meaningful when the intermediate flag is set
    notes
```

**`previous_certificate_id` is excluded from this table for now** (confirmed
— not merely deferred from the form, not created at all yet). It exists in
the spec's data model to let the renewal workflow (step 9) link a new
certificate to the one it replaced, but nothing can populate it meaningfully
before that workflow exists — there's no "previous certificate" to pick
during ordinary data entry. Adding the column now, unused, would be
scaffolding step 9 into step 5. The migration that eventually adds it is
step 9's to write.

**The no-expiry rule is enforced twice.** A `CHECK` constraint in the
migration — mirroring the pattern migration 001 already uses for the
OFFICE/VESSEL ↔ `vessel_id` relationship —

```sql
CHECK (
    expiry_date IS NOT NULL
    OR (requires_annual_survey = 0 AND requires_intermediate_survey = 0)
)
```

— is the backstop. The form is the primary defence (§6): checking "No
expiry" disables and clears both survey checkboxes and the intermediate-mode
picker, so the invalid combination is never something a user can construct
in the first place, only something the schema refuses if it's ever reached
by another route (a bad CSV import in step 11, for instance).

## 5. Screens

Certificates don't split into two screen *classes* by mode the way vessels
did in step 4. A vessel in VESSEL mode is inherently singular — one record,
hence one permanent detail page with no list. A vessel's *certificates* are
not singular even in VESSEL mode — one ship carries dozens. So both modes
use the same `CertificateListWidget`; the only difference is whether
`MainWindow`'s toolbar selector exists above it and which vessel id it's
currently scoped to.

**`ui/CertificateEditForm`** — one `QWidget`, wrapped in a
`CertificateEditDialog` for both Add and Edit, same shape as step 4's
`VesselEditForm`/`VesselEditDialog`. Thirteen fields is a lot for one flat
form, so it's grouped into `QGroupBox` sections — Identity (name, category,
certificate number, applies to), Validity (issue date, expiry date, no-expiry
checkbox, interim checkbox), Issuing authority (issued by, place of issue),
Survey requirements (the two checkboxes and the mode picker), Notes — inside
a `QScrollArea` so it stays usable on a smaller screen. Still no service
layer (§2) — thirteen fields is a lot of UI, not a business rule, and
validation is the same "form disables what shouldn't be enterable, repository
double-checks anyway" pattern as every screen so far.

**`ui/CertificateListWidget`** — a table (Name, Category, Expiry Date), no
colour, no filter, no days-left — those all need `computeCertificateState()`
from step 6. Three states depending on context: OFFICE with no vessel chosen
("Select a vessel above…"), OFFICE or VESSEL with a vessel scoped (the
table), and empty-but-scoped (an actual vessel with zero certificates yet —
just an empty table, that's a normal state, not an error).

## 6. Repository behaviour

`CertificateRepository` mirrors `VesselRepository` closely.

- `list(vesselId, ...)` and `findById(id, ...)` both apply `is_deleted = 0`
  and, in VESSEL mode, silently intersect the requested `vesselId` with
  `InstallationContext::vesselScope()` — a request for a vessel that isn't
  this installation's own returns nothing, the same "not found is a
  successful answer" precedent `VesselRepository::findById()` set.
- `create()`/`update()` refuse outright — with a friendly error, not a
  silent empty result, since these are writes — when the certificate's
  `vessel_id` isn't the installation's own vessel under VESSEL mode.
- `create()`/`update()` re-validate the no-expiry rule before writing
  (friendly message, e.g. "A certificate with no expiry cannot require a
  survey"), the same "friendly pre-check before the raw constraint" pattern
  step 4 established for duplicate IMO numbers — the `CHECK` constraint
  stays as the backstop, not the primary path a user sees an error through.
- `update()` bumps `revision`, refreshes `updated_at`/`updated_by`, leaves
  `created_at`/`created_by` alone — same rule step 4 introduced.

## 7. Edge cases that must have unit tests

1. `list()`/`findById()` in VESSEL mode never return a certificate
   belonging to a different vessel, even one seeded directly (bypassing the
   repository) to simulate a future sync bringing in data it shouldn't
   expose — same test shape as `VesselRepository`'s scope tests.
2. `list()`/`findById()` exclude `is_deleted = 1` rows.
3. `create()`/`update()` rejected under VESSEL mode for any `vessel_id`
   other than the installation's own.
4. `create()` rejects a missing name, missing category, or missing issue
   date.
5. `create()`/`update()` reject `expiry_date` null combined with either
   survey flag true, with the friendly message — and separately, a test
   confirming the raw `CHECK` constraint text never reaches the user.
6. `update()` increments `revision` by exactly one and leaves
   `created_at`/`created_by` untouched.
7. `CertificateEditForm`: checking "No expiry" disables and clears both
   survey checkboxes and the intermediate-mode picker; unchecking it
   re-enables them.
8. `CertificateEditForm`: the intermediate-mode picker is disabled whenever
   "requires intermediate survey" is unchecked.
9. `CertificateListWidget` in OFFICE mode with no vessel selected shows the
   prompt state, not an empty table — and switching the toolbar selector
   reloads the list for the newly chosen vessel.
10. `ModuleRegistry` exposes exactly one module; the sidebar shows "Vessels"
    plus that module's `displayName()`.

## 8. Addendum (found and specified after hands-on testing)

Three small changes, found while the developer tried step 5 for the first
time through the real GUI rather than just unit tests. None of them touch
`computeCertificateState()` or endorsements — step 6 still starts clean
after this.

### 8.1 Defect: the OFFICE toolbar vessel selector doesn't see new vessels

Reproduced: add a vessel from the "Vessels" screen, switch to
"Certificates" — the new vessel isn't in the dropdown until the app is
restarted.

Root cause, confirmed by reading `MainWindow::buildVesselSelector()`: the
dropdown is populated exactly once, from `VesselRepository::list()`, when
`MainWindow` is constructed. Nothing tells it to re-read the list
afterwards. This was invisible until now because nothing outside
`VesselListWidget` needed to know when the fleet changed — the selector is
the first thing that does.

**Fix.** `VesselListWidget` gains a signal, `vesselsChanged()`, emitted
after `addVessel()` or `editVessel()` completes a successful save (i.e.
right after the `reload()` call that already happens in both). `MainWindow`
keeps a pointer to the `VesselListWidget` it builds in `buildSidebar()`
(the same way it already keeps `m_certificates`), and connects that signal,
once `buildVesselSelector()` has created the combo box, to a new
`refreshVesselSelector()` method: re-read the fleet, rebuild the dropdown's
entries, and — this is the part that matters for a smooth experience — if
a vessel was selected before the refresh and it still exists, re-select it
by id rather than by position, since the alphabetical position of other
entries can shift when a new one is inserted.

VESSEL mode is unaffected: it has no selector, and `VesselListWidget` isn't
even constructed there.

**Test:** adding a vessel while the Certificates screen is open and a
different vessel is already selected leaves that selection untouched and
adds the new vessel to the dropdown, without a restart.

### 8.2 A company reference number — "No."

The developer's fleet uses a short reference number per certificate as
company policy (e.g. referring to "Certificate 15D" in a message to a
captain, instead of typing the full certificate name). This needs its own
field — it is not `certificate_number` (the official number on the
document itself, assigned by whoever issued it) and not the internal `id`
(the invisible UUID every row already has).

**Format.** Digits, optionally followed by letters — `"15"`, `"3A"`,
`"15D"` — nothing else, and it may be left blank. The letters-after-digits
rule is not cosmetic: it is what makes sorting correct without writing a
text parser. If this were free text, "15D" would sort *before* "3A",
because plain text comparison reads left to right and `'1'` comes before
`'3'` as a character — exactly backwards from what the number means. By
requiring "digits, then letters, nothing else," the field can always be
split into (leading number, trailing letters) unambiguously, and sorted as
(number, letters) — which is correct by construction, not by careful
parsing.

**Column:** `list_number`, `TEXT`, nullable, on `certificate`. Single free
text field in the UI (not two boxes — tried, rejected as fiddly to fill
in), but the `QLineEdit` carries a validator that simply refuses any
keystroke that would make the field stop matching

```
empty, or: one or more digits, followed by zero or more letters
```

so an invalid character never appears on screen in the first place — the
same "the widget enforces the rule" precedent as the gross tonnage spin box
in step 4 (minimum 0) and the no-expiry checkbox in this step (§4).
`CertificateRepository::validate()` re-checks the same rule as a backstop
before writing, per the project's standing "widget enforces it, repository
double-checks it" pattern — a value could still reach `create()`/`update()`
some other way later (a CSV import in step 11, for instance). The
migration's `CHECK` constraint is the final backstop behind that, the same
three-layer shape already used for the no-expiry rule.

**Placement and sort.** Shown as the first column in
`CertificateListWidget`, labelled "No.", ahead of Name. The list's default
sort changes from Name to `list_number` ascending — this is the whole
point of the field, so it needs to be the default a user sees without
clicking anything, not an option they have to discover. Certificates with
no number set sort to the bottom, so the ones the fleet actually refers to
by number stay together at the top rather than scattered behind blank
entries. Clicking the column header still re-sorts by whichever column was
clicked, same as every other sortable column already.

### 8.3 Issue Date column added to the list

`issue_date` has been stored since migration 003; it just wasn't shown.
Added as a plain column between Name and Expiry Date — no new field, no
migration, formatted the same `dd MMM yyyy` way Expiry Date already is.

**Certificate list column order after this addendum:** No. · Name · Issue
Date · Expiry Date · Category.

(Category moves last rather than being removed — it's still useful for
filtering later, just less important to see at a glance than the dates
next to a reference number the developer already recognises.)

### 8.4 Migration

All of §8.2's schema change ships as `migrations/004_add_certificate_list_number.sql`
— `003_create_certificate.sql` is already committed and, per the project's
migration rule, is never edited once run. `ALTER TABLE certificate ADD
COLUMN list_number TEXT;` plus the `CHECK` constraint from §8.2. Add the
new file to `CMakeLists.txt`'s migration list the same way 001–003 were —
a migration missing from that list compiles into nothing and silently
never runs, per the warning already in `CLAUDE.md` §2.

### 8.5 Tests to add

11. `VesselListWidget::vesselsChanged()` fires after a successful add and
    after a successful edit, and does not fire when the dialog is
    cancelled.
12. `MainWindow`'s vessel selector gains a newly-added vessel without a
    restart, and keeps its current selection (by id) across the refresh.
13. `list_number` accepts `""`, `"3"`, `"3A"`, `"15D"`; rejects (at the
    repository layer) anything containing a character other than a digit
    or letter, and anything where a letter appears before the first digit.
14. Sorting by `list_number` orders `"3"`, `"3A"`, `"3B"`, `"9"`, `"15D"`
    correctly (i.e. numerically by the leading digits, then
    alphabetically by the trailing letters) — the exact case plain text
    sorting would get wrong.
15. Certificates with a blank `list_number` sort after every certificate
    that has one, under the default sort.
