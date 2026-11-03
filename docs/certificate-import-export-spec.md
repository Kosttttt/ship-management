# Certificate CSV import/export and PDF export — Step 13 specification

Version 1.1. Builds on `certificate-control-spec.md` §8 (Import and export)
and §9 (Reporting), which set the original direction; this spec makes every
open decision in those sections concrete, in line with `CLAUDE.md` §11's
build order.

**Changed from v1.0**, after an independent review read the actual source
against every claim this spec makes: the endorsement columns (§6) are now
date *lists*, not single dates — v1.0's single-date design would have made
a correctly-up-to-date, mid-cycle certificate import as falsely Overdue,
because `SurveySchedule::evaluateAnnual()` matches endorsements to
anniversaries one at a time, in order, and a single late date gets claimed
by the *first* anniversary rather than the most recent one. Everything else
below that changed is a factual correction to match code that works
differently than v1.0 assumed — matching now reads `list()` rather than
`findById()` (§5.1), the sort order and "most recent endorsement" wording
are corrected (§4), the dialect and header rules are pinned down (§3), the
contradiction between "read-only" and "writes through the repository" is
resolved (§5.2/§11), and a small domain-layer refactor is named as a
prerequisite (§5.5) rather than assumed to already exist.

## 1. Purpose

Two related needs, both about getting a vessel's certificate list in and
out of the app rather than through the database one dialog at a time:

- **CSV import/export** — bulk initial data entry (loading an existing
  fleet's worth of certificates is far faster in a spreadsheet than one
  dialog at a time), and a way to keep editing that spreadsheet offline and
  bring changes back in.
- **PDF export** — a simple, print-friendly statement of a vessel's
  certificates to hand to a third party (surveyor, Port State Control,
  charterer) on request. A cut-down version of what `certificate-control-spec.md`
  §9 eventually plans as the fuller reporting step (Step 14); this is just
  "the current list, as a document," using the same data CSV export reads.

Both are available in **both installation modes**, each scoped to exactly
one vessel at a time — the one already selected on screen. A VESSEL
installation always operates on its own one vessel; an OFFICE installation
operates on whichever vessel is currently selected in the toolbar, the same
scoping every other screen in this module already uses. There is
deliberately no fleet-wide export or import — one operation, one vessel,
every time. If OFFICE mode has no vessel currently selected, Import/Export
are simply not reachable (disabled/hidden), the same way other per-vessel
actions in this module already behave with nothing selected.

## 2. What a CSV row represents

One row = one certificate's own fields, exactly as `CertificateEditDialog`
edits them today, plus up to two optional columns that become that
certificate's *initial* survey history when the row creates a brand-new
certificate (§6). Nothing else — no extensions, no attachments. Those are
things that happen to a certificate over time and are managed through their
own dedicated UI once the certificate exists.

## 3. CSV column list

| Column (file header)       | Field                              | Required | Notes |
|---|---|---|---|
| `Id`                        | `certificate.id`                   | no | Present on export; blank on a hand-built or fresh row. Never written by import — read-only, used only to find a matching existing row (§5.1). |
| `Vessel IMO`                | —                                   | no | Informational on export; a sanity check on import (§7). Never used to choose which vessel a row belongs to. |
| `Vessel Name`               | —                                   | no | Informational only, never checked. |
| `No.`                       | `certificate.list_number`          | no | Validated by `CertificateListNumber::isValid()` — digits, optionally followed by letters (`15`, `3A`, `15D`). Anything else is an Invalid row (§5.5). Not required to be unique (it isn't in the database either). |
| `Name`                      | `certificate.name`                 | **yes** | |
| `Category`                  | `certificate.category`             | **yes** | `STATUTORY` \| `CLASS` \| `EQUIPMENT` \| `OTHER`, case-insensitive. |
| `Applies To`                | `certificate.applies_to`           | no | |
| `Certificate Number`        | `certificate.certificate_number`   | no | |
| `Issue Date`                | `certificate.issue_date`           | **yes** | `YYYY-MM-DD`, strict — see §5.3. |
| `Expiry Date`                | `certificate.expiry_date`          | no* | `YYYY-MM-DD`, or blank for "does not expire." *Required when either survey column below is true — see the cross-field rule just below the table. |
| `Issued By`                  | `certificate.issued_by`            | no | |
| `Place of Issue`            | `certificate.place_of_issue`       | no | |
| `Interim`                   | `certificate.is_interim`           | no | See §5.4 for accepted boolean spellings. Blank = false. |
| `Notes`                      | `certificate.notes`                | no | |
| `Requires Annual Survey`    | `certificate.requires_annual_survey` | no | Blank = false. |
| `Requires Intermediate Survey` | `certificate.requires_intermediate_survey` | no | Blank = false. |
| `Intermediate Mode`         | `certificate.intermediate_mode`    | no | `ADDITIONAL` \| `REPLACES_ANNUAL`. Blank defaults to `ADDITIONAL` (`certificate-control-spec.md` §3.4's common case), and is ignored entirely when `Requires Intermediate Survey` is false. |
| `Annual Surveys`            | — (initial endorsements, §6)       | no | Semicolon-separated `YYYY-MM-DD` list, e.g. `2027-02-10;2028-02-04;2029-03-01`. Ignored unless `Requires Annual Survey` is true. |
| `Intermediate Surveys`      | — (initial endorsements, §6)       | no | Semicolon-separated `YYYY-MM-DD` list. Ignored unless `Requires Intermediate Survey` is true. |

**Cross-field rule** (mirrors `CertificateRepository::validate()` and the
`003_create_certificate.sql` migration's own `CHECK` constraint, which
names CSV import by name as the reason it exists): a blank `Expiry Date`
together with `Requires Annual Survey` or `Requires Intermediate Survey`
true is an Invalid row, not a silently-accepted one — a certificate with no
expiry has no anniversary to schedule a survey against.

`previous_certificate_id` is **not** a column and cannot be — the migration
that creates the `certificate` table says outright that this field is
deliberately absent until the renewal workflow exists, and nothing in the
schema today can be populated from a spreadsheet cell.

The header row is required and matched by name, **case-insensitively and
trimmed of surrounding whitespace**. A required header (`Name`, `Category`,
`Issue Date`) missing from the file entirely is a **file-level refusal**
before any row is parsed — not 200 Invalid rows. A header present in the
file that doesn't match any name in the table above is also a file-level
refusal, naming the unrecognised header — silently ignoring it is how a
typo'd `Expiry date` (lower-case "d") would quietly blank every row's
expiry and turn a whole vessel's certificates into "does not expire,"
which is exactly the silent-wrong-value failure this project keeps writing
rules to prevent. A recognised header that's simply absent from one
particular file (e.g. a file with no `Notes` column at all) is fine — every
column except `Name`, `Category` and `Issue Date` is optional.

### 3.1 File format

Comma-delimited, RFC 4180 quoting (a field containing a comma, a quote, or
a newline is wrapped in double quotes, with `""` for an embedded quote).
Written with CRLF line endings and a UTF-8 byte-order mark; read accepting
either CRLF or LF, and skipping a BOM if present rather than requiring one.
The BOM specifically is not optional on write — Excel on Windows will
otherwise mojibake non-ASCII text (this project has already spent real time
this week on the identical BOM/encoding problem in git commit messages, for
the same underlying reason). Qt ships no CSV parser, so this is hand-rolled
regardless of these choices; there is no third-party dependency implied.

## 4. Export

Reads the selected vessel's certificates (`is_deleted = 0`) via
`CertificateRepository::list()`, then **sorts the results by list number**
using the same comparison `ListNumberItem`/the certificate list screen's
own sort already applies (numeric-then-alphabetic, blanks last) —
`list()` itself returns rows ordered by name, not list number, so the
exporter must apply this sort explicitly rather than assume the repository
already has. Writes one row per certificate with every column in §3 filled
from a **live read at the moment of export** — never a cached or
import-time value. `Annual Surveys`/`Intermediate Surveys` are built by
reading that certificate's endorsements via `EndorsementRepository::list()`
and collecting every non-deleted date of the matching type, in date order
— there is no dedicated "most recent" accessor on the repository, so the
exporter does this collection itself. If a certificate's survey history has
moved on since it was first imported, exporting again shows every survey
recorded since, not just whatever was true at import time.

`Id` is always populated on export — this is what makes an export-edit-reimport
round trip (§5) possible without the office having to invent its own
matching scheme.

## 5. Import

### 5.1 Matching

The selected vessel's certificates are read **once**, via
`CertificateRepository::list(selectedVesselId, ...)`, into memory before any
row is matched. A row's `Id` is checked against that in-memory set only —
**never** via `CertificateRepository::findById()`, which in OFFICE mode
applies no vessel filter at all and would happily return (and, on
overwrite, silently corrupt) another vessel's certificate if its `Id`
happened to appear in the file. Reading the scoped list once and matching
against it is inherently safe by construction, and it's the same read
export already performs (§4).

`Id` blank, or `Id` non-blank but not present in that vessel's list
(including an `Id` that names a real certificate belonging to a *different*
vessel, or one that has since been soft-deleted — `list()` excludes both):
the row is a **new** certificate. `Id` present and found: the row is a
**match**, and the user is asked, per row, to **skip** or **overwrite** —
exactly the behaviour `certificate-control-spec.md` §8 already describes.
No other column (certificate number, name, list number) is ever used for
matching — none of them are guaranteed unique, and a coincidental match
would silently overwrite the wrong certificate.

A new row whose `Id` matched a *soft-deleted* certificate is shown in the
preview as **"New (previously deleted)"** rather than plain New, so this
doesn't read as a bug when someone notices the resurrected certificate gets
a different `Id` than the one in the file (`create()` always mints a fresh
UUID; it has no way to reuse one supplied from outside).

### 5.2 The write path

Every row is written through `CertificateRepository::create()`/`update()`
and, for the endorsement columns (§6), `EndorsementRepository::create()` —
never a direct `INSERT`/`UPDATE` from the importer — so CSV import goes
through the exact same validation, scope enforcement and audit-column
handling every dialog already relies on (`CLAUDE.md` §4 rule 2: SQL lives
only in the repository layer). The import service composes these two
repositories exactly like every other service in this module; the one
thing it additionally needs that no existing service does is a
`QSqlDatabase&`, used for nothing but a small RAII transaction scope around
the whole row loop. This is safe to nest: neither `create()` nor `update()`
opens a transaction of its own (only `softDeleteWithChildren()` and the
attachment repository do), so the outer transaction is the only one in
play. The whole import either fully succeeds or changes nothing
(`certificate-control-spec.md` §8).

This is also why the existing technical-debt note about
`certificate.vessel_id` having no foreign key is resolved without touching
the schema: the importer never derives a `vessel_id` from file content at
all (§7) — every row is written against the vessel already selected on
screen, the same validated id every other write path already uses. There
is no new unvalidated entry point, so there is nothing left for a foreign
key to protect against here.

### 5.3 Dates

Strict `YYYY-MM-DD`. A cell in any other format (including whatever Excel's
own autocorrect may have turned a typed date into) is a row-level
validation failure (§5.5), not a best-effort parse — silently guessing a
locale is how a `02/03/2026` cell becomes the wrong day for someone, and
this project has already been burned once by a confident-looking command
doing the wrong thing.

### 5.4 Booleans

`Interim`, `Requires Annual Survey` and `Requires Intermediate Survey`
accept (case-insensitive) `true`/`false`, `yes`/`no`, `y`/`n`, or `1`/`0`;
blank is `false`. Anything else is a row-level validation failure.

### 5.5 Validation and the preview screen

**Prerequisite, confirmed by reading the code rather than assumed:**
`CertificateRepository::validate()` is private today, and
`CertificateEditDialog` never calls a validator at all — it calls
`create()`/`update()` directly and shows `errorString()` in a message box
on failure. There is currently nothing a preview screen could call
*before* writing. This step needs that logic pulled out into a domain-layer
free function (e.g. `validateCertificate(const Certificate&, QString*
reason)`, beside `CertificateCodes`, per `CLAUDE.md` §4's domain layer
owning business rules) that `CertificateRepository::create()`/`update()`
call as their first step — keeping the write-path backstop exactly as
strict as it is today — and that the CSV previewer also calls, so "the same
rule the dialog enforces" is actually true rather than two rules that can
drift. Small, no behavioural change, and it's the one piece of production
code this step needs touched outside its own new files.

Before anything is written, every row is parsed and checked against that
one validator, plus the CSV-specific checks in §3/§5.3/§5.4 that have no
dialog equivalent (header names, date/boolean parsing, the semicolon-list
syntax in §6). The user then sees a preview table, one row per CSV row,
each marked:

- **New** (or **New (previously deleted)**, §5.1) — will be created.
- **Match found** — an existing certificate; a per-row choice of skip or
  overwrite, defaulting to skip.
- **Invalid** — a reason shown inline (missing required field, unparseable
  date, unknown category, the cross-field rule, an invalid `No.`, etc.).

Every row ends up in exactly one of those three states — there is no fourth
"excluded" state. The whole import is blocked, with nothing written, while
any row is Invalid; once every row is New or a resolved Match, importing
runs entirely inside the one transaction from §5.2.

## 6. The endorsement columns, precisely

`Annual Surveys` and `Intermediate Surveys` take effect **only** when a row
is creating a brand-new certificate (including "New (previously deleted)").
On a matched row that the user chooses to overwrite, both columns are read
from the file (so they can still be shown in the preview for the office's
own reference) but are **never written** — overwrite touches only the
certificate's own fields in §3's table, never endorsements. This is
deliberate, not an oversight: it means re-importing the same export twice,
or an export that was later edited and re-imported, can never create a
duplicate survey record, and it keeps "how a correction to recorded survey
history is made" to exactly one place — the endorsement edit/delete UI
already built — rather than two.

**Why a list, not a single date.** `SurveySchedule::evaluateAnnual()`
walks the certificate's anniversaries in order and lets each one claim the
*earliest* still-unclaimed endorsement of the matching type — it is not
"the most recent survey determines what's next," it is "every past survey
accounts for one past anniversary, in order." A certificate three years
into a five-year term, with its first three annuals actually done, needs
all three dates recorded or the matching walk has nothing to claim the
first two anniversaries with and reports them Overdue, with a fabricated
"completed late" note attached to whichever anniversary the one date it
does have gets matched to instead. Supplying every real historical date
feeds the existing, unmodified matching algorithm exactly what it already
expects from live data entry — there is no special-casing for
`REPLACES_ANNUAL` needed here either: an intermediate-type date in the list
that falls inside the 2nd or 3rd annual window is picked up by
`evaluateAnnual()`'s own existing merge logic the same way a live-entered
one would be.

Each date in either list becomes an ordinary `EndorsementRepository::create()`
call (`survey_type` `ANNUAL`/`INTERMEDIATE` respectively), validated
exactly as the Add Endorsement dialog validates one today — including "not
before the certificate's issue date." If any date in a list fails that
validation, the whole row is Invalid at preview time (§5.5), not partially
imported.

## 7. Vessel scope and the sanity-check column

The vessel a row belongs to is **never** read from the file. It is always
the vessel already selected on screen before Import or Export is opened —
trivial in VESSEL mode (there is only one), and whichever vessel is
currently selected in OFFICE mode. `Vessel IMO`/`Vessel Name` are written
on export purely so the file is self-identifying if someone opens it later,
out of context.

On import, if `Vessel IMO` is present on a row and does **not** match the
currently-selected vessel's own IMO number, the **entire import is
refused** before anything is written, with a message naming the mismatch —
not a per-row skip, since a file carrying another ship's IMO number usually
means the wrong file was opened, not that one row was miskeyed. A row with
`Vessel IMO` blank is not checked (a hand-built file reasonably has no
reason to carry it). This check cannot itself be bypassed by having no
vessel selected, since Import/Export aren't reachable in that state (§1).

## 8. PDF export

A simple, print-friendly document for one vessel, built the same way
`certificate-control-spec.md` §9 already specifies for every report in this
project — `QTextDocument` + `QPrinter` — reading the same live certificate
list as CSV export (§4), in the same sorted order, through the same four
collaborators `CertificateListWidget` itself already needs to compute each
row's current status: `CertificateRepository`, `EndorsementRepository`,
`CertificateExtensionRepository`, and `AppSettingRepository` (for the
`AlertThresholds` `computeCertificateState()` takes).

Contents: vessel name and IMO number as a header, a generation timestamp
(date and time the document was produced, read at the moment of export —
exactly like the CSV's own live-read rule), then one row per certificate
showing its name, category, certificate number, issue date, expiry date,
survey window dates (when a survey is outstanding), and current status —
the same columns the certificate list screen itself shows, laid out for a
printed page rather than an on-screen table. No endorsement history, no
filtering by status — every non-deleted certificate for the vessel, since
the point of handing this to a third party is a complete statement of what
is on board and its current standing, not a curated subset.

`QPrinter` requires the `Qt6::PrintSupport` component, which nothing in
this project links today — `CMakeLists.txt`'s `find_package(Qt6 REQUIRED
COMPONENTS Widgets Sql)` needs `PrintSupport` added, and whichever target
builds the PDF needs it linked. Not a new third-party dependency —
`CLAUDE.md` §2 names `QTextDocument + QPrinter` outright — but it is a
build-file change worth calling out rather than discovering at link time.

## 9. Edge cases to test

1. A round trip: export a vessel's list, change nothing, re-import — every
   row is a Match; choosing overwrite for all of them leaves the database
   unchanged **except** `revision`, `updated_at` and `updated_by`, which
   still move since it's an ordinary `update()` call regardless of whether
   the values actually differ.
2. A fresh, hand-built CSV with no `Id` column at all — every row is New.
3. A row whose `Id` names a real certificate on a *different* vessel than
   the one selected — treated as no match (not found, not "another
   vessel's certificate found"), consistent with every other scope-filtered
   read in this project. Confirms the fix in §5.1 (reading the scoped list
   once) rather than the `findById()` approach this version moved away from.
4. A row whose `Id` names a *soft-deleted* certificate — shown as "New
   (previously deleted)," not plain New and not a Match; the resulting new
   row has a different `Id` than the file's, and the old deleted row is
   untouched.
5. `Vessel IMO` present and wrong on even one row — whole import refused,
   nothing written, including the valid rows. Also: the file carries a
   `Vessel IMO` while the currently selected vessel's own IMO is somehow
   blank/unconfigured — treated as a mismatch, refused, not silently
   allowed through.
6. The same `Id` appearing twice in one file — a file-level refusal, not
   "first one wins" or "last one wins."
7. A required header (`Name`, `Category`, or `Issue Date`) missing from the
   file entirely — a file-level refusal before any row is parsed. A header
   present that matches nothing in §3 (e.g. a typo) — also a file-level
   refusal, naming it.
8. A new-certificate row with both survey-list columns filled, where the
   certificate requires both tracks, each list holding multiple dates —
   every date becomes its own endorsement; the resulting `endorsement`
   row count for that certificate equals the total number of dates across
   both lists, not one, not deduplicated incorrectly.
9. A new-certificate row with a survey-list column filled for a track the
   certificate does *not* require — the column is ignored, not treated as
   an error.
10. A matched row, chosen as overwrite, with survey-list columns filled —
    the certificate's own fields update; `EndorsementRepository` row count
    for that certificate is unchanged.
11. A new-certificate row where `computeCertificateState()`, run against
    the freshly-created endorsements, reports the correct current severity
    for a certificate genuinely mid-cycle (e.g. 3 of 4 annuals already
    done, next one still in the future) — this is the test that would have
    caught v1.0's bug, so it needs to exist explicitly, not just be implied
    by the round trip.
12. Blank `Expiry Date` with either survey-requirement flag true — Invalid,
    with the cross-field reason shown, not silently accepted.
13. An invalid `No.` (e.g. `A1`, `15/D`) — Invalid with the same message
    `CertificateRepository::validate()` already gives for this.
14. An invalid date format, an unknown category string, and a missing
    required field on different rows — each surfaces as an Invalid row
    with a distinct, specific reason, and blocks the whole import exactly
    as a parse failure should.
15. `Notes` (or any free-text column) containing a comma, a double quote,
    and an embedded newline, each in a separate test — written and then
    read back unchanged, proving the RFC 4180 quoting in §3.1 round-trips.
16. `Place of Issue` (or any column) containing non-ASCII text — written
    with the UTF-8 BOM and read back correctly, both by this importer and
    by opening the file in a plain text check that confirms the BOM bytes
    are actually present.
17. Export, then record a new endorsement through the normal UI, then
    export again — the second file's `Annual Surveys`/`Intermediate
    Surveys` include the new date, proving export is a live read and not a
    cached value from the first export or from import time.
18. PDF export with zero certificates on the vessel, and with one that
    never expires (`expiry_date` null) — both render without error; a
    never-expiring certificate shows no survey window, consistent with the
    certificate list screen's own blank-cell behaviour.
19. VESSEL-mode import/export, confirming scope is automatic (no vessel
    picker exists in that mode, same as every other VESSEL-mode screen).
20. OFFICE mode with no vessel currently selected — Import/Export are not
    reachable at all.

## 10. Out of scope

- Fleet-wide export/import — one vessel at a time, always (§1).
- `.xlsx` — CSV only, per `CLAUDE.md` §2's "CSV first, `.xlsx` only if CSV
  proves insufficient."
- Extensions and attachments in the CSV — certificate fields and initial
  endorsement history only (§2).
- A real foreign key on `certificate.vessel_id` — resolved without a schema
  change, since import never writes an unvalidated vessel id (§5.2).
- Any rework of the endorsement model itself (keeping only the most recent
  endorsement per track rather than full history) — a separate, substantial
  decision, deliberately deferred to its own design discussion after this
  step, not decided as a side effect of this spec. Note for that future
  discussion: this step's `Annual Surveys`/`Intermediate Surveys` columns
  store full history in the database exactly as live data entry would,
  regardless of how many dates a row's list holds — if the rework later
  collapses stored history down to "most recent only," that's a migration
  decision independent of how this step populated it.
- The fuller reporting step (`certificate-control-spec.md` §9: fleet-wide
  compliance, expiring-within-a-period, overdue-only, full per-certificate
  history) — Step 14, unaffected by this one beyond sharing the same
  `QTextDocument`/`QPrinter` mechanism.

## 11. Files expected to change (confirmed exactly once implementation starts)

- `modules/certificates/domain/` — the new `validateCertificate()` free
  function extracted from `CertificateRepository::validate()` (§5.5), and
  `CertificateRepository`/`.cpp` updated to call it.
- `modules/certificates/service/` — a new service for parsing, validating
  and importing/exporting rows, and the PDF builder. The import side holds
  a `QSqlDatabase&` for its own transaction scope (§5.2); everything else
  composes repositories only, same as every other service in this module.
- `modules/certificates/data/EndorsementRepository` — no new methods
  expected; `create()` and `list()` already cover everything this step
  needs.
- `modules/certificates/ui/` — the Import/Export entry points (likely
  alongside `CertificateListActions`), the row-preview dialog for import,
  and whatever triggers the PDF's print/save dialog.
- `CMakeLists.txt` — add `PrintSupport` to `find_package(Qt6 ...)` and link
  it into whichever target builds the PDF (§8).
- Test suites for all of the above, plus the new CSV-parsing logic in
  isolation from any widget.
