# Vessel Archive / Restore — Specification

Version 1.0. This is the "small follow-up" `docs/vessel-crud-spec.md` §2
explicitly deferred: "removing a vessel from the fleet," parked back then
because nothing referenced a vessel yet and there was no real rule to design
it against. The certificates module exists now, so this is that follow-up,
on schedule rather than late.

---

## 1. Why, and why "archive" rather than "delete"

`CLAUDE.md` §6 rule 4 bans a real `DELETE FROM` everywhere in this project,
for the same reason it always has: an audit trail has to survive, and a
deletion has to be able to replicate to the other side once sync exists.
So there is only ever one action here, not two — what changes is what to
call it. **Archive** is the name used throughout this spec, deliberately,
because it is the honest description of what happens: nothing is destroyed,
no certificate history is touched, and it is fully reversible. A UI label
of "Delete" would describe something this project does not do.

`is_deleted` has sat on the `vessel` table since migration 002, filtered
out of every query, with the comment in `VesselRepository::list()` saying
plainly that nothing can set it yet. This spec is what sets it.

## 2. Scope

In scope:
- `VesselRepository::archive()` and `VesselRepository::restore()`.
- A way to see archived vessels again (a toggle in the vessel list) — built
  now, not deferred, for the reason in §5.
- The one-row UI for both actions on `VesselListWidget` (OFFICE only — see
  §3).

Explicitly out of scope:
- Any cascade to `certificate`, `endorsement`, `certificate_extension`, or
  `certificate_attachment`. §4 explains why none is needed.
- Anything to do with `attachment_root_path`. It is left exactly as
  configured; archiving does not move, hide, or touch the vessel's
  attachment folder.
- Role-based access beyond the existing OFFICE/VESSEL split — RBAC proper
  is still deliberately last (`CLAUDE.md` §11).
- CSV import/export behaviour. Nothing here changes it; an archived
  vessel simply cannot be selected to import or export against, for the
  same structural reason given in §4.

## 3. OFFICE only — enforced in the repository, not just hidden

Exactly the precedent `VesselRepository::create()` already set: refused
outright at the repository level, not merely absent from a VESSEL
installation's screen.

```cpp
if (isScopedToOneVessel()) {
    m_errorString = QStringLiteral(
        "A vessel installation cannot archive or restore a vessel.");
    return false;
}
```

A VESSEL installation may not archive *any* vessel, including itself —
this is fleet management, the same category `create()`'s comment already
puts vessel creation in, not day-to-day work for the crew on board.

## 4. No cascade needed — and why that's provably true, not assumed

This is the one finding worth stating plainly before the rest of the
design, because it's what keeps this spec small: **archiving a vessel
needs to touch only the `vessel` row.** Nothing downstream needs its own
`is_deleted` flipped.

The reason: every path that reaches a vessel's certificates gets there by
first calling `VesselRepository::list()` or `::findById()`, both of which
already exclude `is_deleted = 1` rows. That's true of:
- `VesselListWidget` itself (the vessel selector in the certificates
  module toolbar, mentioned in its `vesselsChanged()` signal, pulls from
  the same repository call).
- `CertificateAlertProvider::attentionByVessel()`, which loops
  `for (const Vessel& vessel : fleet)` over exactly that list before
  reading any certificate — so an archived vessel's certificates stop
  being swept for alerts and stop contributing to the sidebar badge,
  automatically, the moment the vessel itself is archived.

So archiving hides a vessel's certificates as a structural consequence of
how the app already finds them, not because this spec reaches into the
certificates module (which it must not — `CLAUDE.md` §5: no module
`#include`s another's `data/`). Restoring is just as clean: nothing
downstream was ever touched, so the vessel's certificates, endorsements,
extensions, and attachments reappear exactly as they were the moment
`is_deleted` flips back to 0.

**Verify this claim, don't just trust it written down here:** once built,
confirm with a real test (see §8) that an archived vessel's certificates
genuinely stop appearing in `attentionByVessel()`'s output, and genuinely
reappear after a restore.

There is a second case worth the same scrutiny, scarier than the alert
sweep because it's live on screen rather than in the background:
**archiving the vessel currently selected in the certificates toolbar.**
This already has an answer in the existing code, not a new one this spec
has to invent — `MainWindow::refreshVesselSelector()` re-finds the current
selection by id, gets back "not found," and explicitly calls
`m_certificates->setVesselId(QString())`, with a comment saying this also
covers a selection that no longer exists. So the certificates screen
blanks cleanly the moment its vessel is archived, for free. Worth its own
line in §8 precisely because it already works — the job here is confirming
it keeps working once `archive()` exists, not building it.

## 5. Why Restore is built now, not deferred

Worth deciding deliberately rather than by default, and decided here:
build the restore path in this same step. Once a vessel is archived,
`list()` never returns it again — with no restore UI, getting it back
would mean asking Claude Code to hand-edit the database. For an app whose
whole purpose is never losing track of safety-critical certificate data,
shipping a hide-with-no-way-back action as the default is the wrong call,
even though it would be the smaller patch.

## 6. Data model changes

### `core/Vessel.h`

Add one field:

```cpp
bool isArchived = false;
```

Named for what it displays, the same way every other field here already
translates a snake_case column into something the UI-facing code reads
naturally (`imoNumber`, not `imo_number`) — this one happens to be backed
by the existing `is_deleted` column, not a new one.

### `core/VesselRepository.h/.cpp`

**Mechanical warning before anything else:** `kSelectColumns` and
`vesselFromRow()` are positional — today's row reads `value(0)` through
`value(7)` in exactly the order `kSelectColumns` lists them. `is_deleted`
must be **appended at the end**, as `value(8)`, not inserted into the
middle of the list where it might read more naturally next to other
status-ish columns. Inserting it anywhere but last silently shifts every
field after it one position, and nothing about that mistake would look
wrong at a glance — every existing field would just start reading the
column to its own right. `create()`'s `INSERT` and `update()`'s `UPDATE`
build their own explicit column lists and are unaffected by this.

`list()` and `findById()` both gain an `includeArchived` parameter,
**defaulting to `false`:**

```cpp
bool list(QList<Vessel>* vessels, bool includeArchived = false);
bool findById(const QString& id, std::optional<Vessel>* vessel,
              bool includeArchived = false);
```

Defaulting to `false` means every existing call site —
`CertificateAlertProvider`, the certificates toolbar's vessel selector,
`VesselListWidget`'s normal view, anywhere else that already calls either
method — keeps behaving exactly as it does today with no change to its own
code. Only the new "show archived" toggle in §7 passes `true`.

Two new methods, matching the shape `update()` already set, but **pre-read
before writing, not left to `numRowsAffected()` alone** — the same choice
`CertificateRepository::softDeleteWithChildren()` already makes, loading
the row first "to be sure the row is really there before anything is
written." `update()` can get away with one combined "no longer exists"
message because there's only one way for its `WHERE` to match zero rows.
`archive()` and `restore()` each have two different zero-row reasons (an
unknown id, or the vessel already being in the state being asked for), and
those deserve two different messages, not one that's wrong half the time:

```cpp
// OFFICE only (§3). findById(vesselId, &existing, /*includeArchived=*/true)
// first: "This vessel is no longer in the database." if not found at all,
// "This vessel is already archived." if found but existing->isArchived.
// Otherwise sets is_deleted = 1, bumps revision/updated_at/updated_by, same
// as update().
bool archive(const QString& vesselId);

// OFFICE only (§3). The mirror image: same pre-read, "not archived" instead
// of "already archived" when found but !existing->isArchived. Before
// writing, also re-run the same IMO-number uniqueness check create()/
// update() already use — see the note below for why this one actually
// matters — and fail with the same friendly message ("This IMO number
// already belongs to another vessel.") rather than a raw constraint error.
bool restore(const QString& vesselId);
```

**Correction, caught during implementation, not before.** The first
version of this paragraph claimed archiving a vessel frees its IMO number
for reuse, because `isImoNumberTaken()` filters to `is_deleted = 0`. That
was wrong: migration 002 declares `imo_number TEXT NOT NULL UNIQUE` as a
plain table constraint with no `is_deleted` condition, so the database
itself refuses the reuse regardless of what the application-level check
says. Claude Code's first test asserted this paragraph's claim and failed
with `UNIQUE constraint failed: vessel.imo_number`; the test now pins what
actually happens (`anArchivedVesselStillHoldsItsImoNumber()`).

The database is the one that's right, domain-wise — an IMO number belongs
to a hull for its whole life, archived or not. What's left over is a
mismatch this round introduced: before it, nothing could set `is_deleted`,
so `isImoNumberTaken()`'s filter and the table's constraint could never
disagree. Now they can, and adding a new vessel that reuses an archived
one's IMO number shows the raw SQLite error instead of the friendly
message. Tracked as an open decision in `docs/roadmap-and-pending.md`, not
silently fixed here. `restore()`'s own IMO check is kept as a backstop
that, given the constraint, can't currently be reached.

## 7. UI — `VesselListWidget`

Kept to the same minimal style the original screen was built with
(`CLAUDE.md` §10 — no abstraction without a reason; a `QTableWidget`, not a
model/view pair, per that file's own existing comment).

- A checkbox above the table, **"Show archived vessels"**. Unchecked by
  default. Toggling it calls `reload()` with `includeArchived` set
  accordingly.
- Archived rows, when shown, render visibly differently from active ones,
  so there is never a moment where an archived and an active vessel look
  the same. **Correction, caught during implementation:** this bullet
  originally offered `setEnabled(false)` on the row's items as one way to
  do it. That option doesn't work — a disabled `QTableWidgetItem` can't be
  selected, so an archived row could never be selected to press Restore.
  As built: greyed and italic text, plus a Status column that only appears
  while "Show archived vessels" is ticked, so the ordinary fleet list looks
  exactly as it did before.
- One context-sensitive button, not two — matching the screen's existing
  single `&Add Vessel` button rather than growing a row of controls for a
  single-selection table. Its label and action depend on the selected
  row's `isArchived`:
  - Active vessel selected → **`&Archive…`**, calling `archive()`.
  - Archived vessel selected → **`&Restore`**, calling `restore()`.
  - Nothing selected → disabled, same pattern `CertificateDeleteActions`
    already uses for its own button's enabled state.
- **Double-click continues to open the edit dialog for an active vessel
  only.** An archived row does not open for editing — it is frozen until
  restored. **Do not implement this as "let `editVessel()` call
  `findById()` and see what comes back"** — with `includeArchived`
  defaulting to `false`, that call returns nothing for an archived row and
  `editVessel()`'s existing fallback says *"This vessel is no longer in
  the database,"* which is false and alarming; the vessel is right there,
  just archived. `editVessel()` needs its own explicit check — the row's
  `isArchived`, read off the same item data `kVesselIdRole` already rides
  on — and either does nothing or says plainly "This vessel is archived.
  Restore it to make changes," before ever calling `findById()`.
- **Both `archive()` and `restore()`, on success, must `emit
  vesselsChanged()`** — the same signal `addVessel()` and `editVessel()`
  already emit. This is not optional polish: it is the only thing keeping
  the certificates toolbar's vessel selector in step with the fleet
  (`certificate-crud-spec.md` §8.1 is the defect report from the last time
  something changed the fleet without it). Skip this and the repository
  layer is perfectly correct while the screen silently breaks §4's whole
  promise — an archived vessel stays selectable in the toolbar and keeps
  showing its certificates until something unrelated happens to trigger a
  refresh.

### Confirming Archive

One vessel at a time — the table is `SingleSelection`, so there is no bulk
path to design here, unlike the certificate list's multi-select delete.
Follow `CertificateDeleteActions::deleteOne()`'s existing wording shape: a
named question, then a plainly-stated consequence, then the ask:

```
Archive 'V UNO'?

Its certificates will not be deleted — archiving only hides the vessel and
its certificates from the fleet list and from alerts, and both can be
brought back at any time with Restore.
```

**No certificate count in this message, and this is a deliberate change
from the first draft of this spec, not an oversight.** A count would need
`VesselListWidget` — today a pure shell screen taking only
`VesselRepository&` — to also depend on `CertificateRepository`, which
reaches a fleet-management screen into a feature module's data layer for
a number the sentence doesn't actually need to make its point. It also
doesn't scale: the day a PMS or DMS module exists, this same dialog would
want their counts too, and `IModule` has no generic hook for "what do you
hold against this vessel," only `alertProviders()`. The sentence above
does its job — reassurance, not arithmetic — without the count or the
dependency. This is also the direct answer to the question
`vessel-crud-spec.md` §2 originally left open ("does it check for open
certificates?"): no, because nothing is at risk of being orphaned or lost,
so there is nothing to block on.

### Confirming Restore

No confirmation dialog. Restoring is the undo of Archive, costs nothing if
done by "mistake" (the vessel can simply be archived again), and a second
question in front of an already-reversible action would just be friction.

## 8. Tests

Split across the existing two files, the same way the existing vessel CRUD
tests already are (`VesselTestSupport.h` for shared setup):

`tst_VesselRepositoryScope.cpp`:
- `archive()`/`restore()` both refused for a VESSEL-scoped installation,
  including against its own vessel id.

`tst_VesselRepositoryWrites.cpp`:
- `archive()` sets `is_deleted = 1`, bumps `revision`, refreshes
  `updated_at`/`updated_by`; fails with a distinct message for an unknown
  id versus one already archived (§6's pre-read is what makes these two
  cases distinguishable — don't let this collapse back to one message).
- `restore()` is the mirror image — distinct "unknown id" vs. "not
  archived" messages — and fails cleanly on the real case from §6: one
  whose IMO number has since been taken by another vessel.
- `list()`/`findById()` default to excluding archived rows, and return
  them when `includeArchived = true` is passed explicitly.

Two tests outside `VesselRepository`, proving §4's central claim rather
than just asserting it in a comment:
- After archiving a vessel with at least one certificate due for a survey,
  `CertificateAlertProvider::attentionByVessel()` no longer includes it —
  and does again after `restore()`.
- Archiving the vessel currently selected in the certificates toolbar
  results in the certificates screen showing nothing (empty `vesselId`),
  via the existing `refreshVesselSelector()`/`setVesselId(QString())` path
  in `MainWindow.cpp` — confirming that path still fires correctly once
  `archive()` exists, not building new behaviour for it.

## 9. What this spec does not change

- The attachment folder a vessel points at — untouched either way.
- Anything in the certificates module's own tables or code.
- The IMO-number validator, CSV format, or report builder.
- Role-based access control, still deliberately last.

## 10. Follow-up round — IMO message and file sizes

Decided with the developer after the first round shipped (`11efcd9`).

### 10.1 Make the IMO check agree with the database

`VesselRepository::isImoNumberTaken()` stops filtering on `is_deleted`, so
it sees exactly what migration 002's `UNIQUE` constraint sees. When the row
holding the number is an archived vessel, the message names it and points
at the fix, instead of the generic one:

- `create()`: *"This IMO number belongs to the archived vessel '%1'. Tick
  "Show archived vessels" and restore it instead of adding it again."*
- `update()`: *"This IMO number belongs to the archived vessel '%1'."*
  (no restore hint — someone editing a vessel isn't trying to add one).
- An active holder keeps today's message unchanged.

How the check reports "who holds it and are they archived" is Claude
Code's call (extra out-parameters, or returning the holder). `restore()`'s
own check stays as a backstop, with its comment updated to say why it
still can't be reached.

Tests: rework `anArchivedVesselStillHoldsItsImoNumber()` so it asserts
`create()` fails with the archived-vessel message — the error must contain
the archived vessel's name and must **not** contain `UNIQUE constraint`.
Add the same for `update()`. The existing duplicate-IMO tests for active
vessels must still pass unchanged.

### 10.2 Bring the two files back under ~300 lines

- **`VesselListWidget.cpp` (319):** extract `VesselArchiveActions`, the
  same shape as `CertificateDeleteActions` — it owns the Archive/Restore
  button, its enabled state, the confirmation, and both actions. The
  widget must still emit `vesselsChanged()` after either one. Re-run the
  vacuity check from the first round after the move (temporarily remove
  the emit, confirm `archivingAVesselRemovesItFromTheSelector()` fails,
  restore it), because moving the code is exactly how that emit gets lost.
- **`VesselRepository.cpp` (527):** `archive()` and `restore()` share one
  private helper for the common part (scope refusal, pre-read, flag flip,
  revision bump), keeping the class in one file. If that alone doesn't
  bring it under ~300, report the number rather than splitting one class
  across two files.

No behaviour change in 10.2 — every existing test passes as it is.
