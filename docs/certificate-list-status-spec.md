# Certificate List — Status, Survey Window & Days-Left Spec

Step 7 of the build order (`CLAUDE.md` §11). Wires the already-built, already-tested
`computeCertificateState()` (step 6) into the certificate list screen, so status,
survey timing and days remaining are visible without opening each certificate.

Read alongside `docs/certificate-control-spec.md` §3–4 (the rules being displayed
here) and `docs/certificate-endorsement-spec.md` (where `computeCertificateState()`
and `Endorsement` were built).

## 1. Purpose

Right now `CertificateListWidget` shows five static columns and nothing tells the
user, at a glance, which certificates need attention. This step adds that, using
the domain logic that already exists — **no new business rules, no schema change,
no new repository method.** This is purely: call the existing pure function once
per row, and render what it returns.

## 2. Scope

In scope:
- New column layout for `CertificateListWidget`.
- A `Status` column: text label + background colour per `DisplayStatus`, with a
  custom severity-based sort.
- `Survey From` / `Survey To` columns.
- A `Days Left` column, always populated.
- A single "needs attention" filter checkbox.

Out of scope, deferred:
- Editable alert thresholds — this step calls `computeCertificateState()` with
  `AlertThresholds()` (hardcoded 30/60/90 defaults). Step 8 (Settings) makes
  these editable; this screen doesn't change when that happens, only where the
  thresholds value comes from.
- Sidebar badge / daily toast — step 9 (Alerts).
- Filtering by category, by specific status, or by vessel (OFFICE mode already
  has the toolbar vessel selector for that).
- Any visual theming/dark-mode work beyond "Valid means no highlight."

## 3. Column layout

Replacing the step-5 layout (`No. · Name · Issue Date · Expiry Date · Category`)
with:

```
No. · Name · Status · Expiry Date · Survey From · Survey To · Days Left
```

`Issue Date` and `Category` are dropped from the list (they remain in the edit
dialog — nothing is lost, just not shown in a screen whose job is now "what
needs attention"). `Status` sits right after `Name` since it's the column a user
scans first.

## 4. Status column

One call to `computeCertificateState()` per certificate gives a `DisplayStatus`.
Render it as text + a background colour, per this table (agreed with the
developer directly, overriding an earlier draft that grouped some of these):

| DisplayStatus   | Label            | Colour                                    |
|------------------|------------------|--------------------------------------------|
| `Expired`        | "Expired"        | Red                                       |
| `SurveyOverdue`  | "Survey Overdue" | Red                                       |
| `Critical`       | "Critical"       | Red                                       |
| `ExpiringSoon`   | "Expiring Soon"  | Orange                                    |
| `SurveyDue`      | "Survey Due"     | Yellow                                    |
| `DueSoon`        | "Due Soon"       | Light green                               |
| `Valid`          | "Valid"          | **No highlight** — default row background |

All seven labels stay visually distinct as *text*, even though three share red as
a background colour — this keeps the screen readable in print (no colour at all)
and for anyone who can't distinguish red from orange from yellow: the word is
always there regardless of colour.

Suggested starting colours (pastel background + a readable darker foreground of
the same hue, not solid saturated fills — easier to read at table-row density and
prints without soaking the page):

- Red: background `#F8D7DA`, text `#842029`
- Orange: background `#FFE5CC`, text `#7A4100`
- Yellow: background `#FFF3CD`, text `#664D03`
- Light green: background `#E6F4EA`, text `#1E7A34`
- Valid: no background change, default text colour

These are a reasonable starting point, not a hard requirement — adjust once it's
actually on screen if something reads poorly against the rest of the UI.

### Sorting the Status column

A custom `QTableWidgetItem` subclass, the same pattern as step 5's
`ListNumberItem`, sorting by severity rank rather than alphabetically. Rank
follows `DisplayStatus`'s own declared order (`Valid` best, `DueSoon`,
`SurveyDue`, `ExpiringSoon`, `Critical`, `SurveyOverdue`, `Expired` worst) — so
clicking the header once (ascending) shows `Valid` first, most urgent last;
clicking again (descending) surfaces the most urgent certificates first. This
mirrors how ascending already reads for every other column (increasing order),
rather than inventing a special case.

The list's **default sort stays `No.` ascending**, unchanged from step 5 — this
matches the company numbering convention already established as important.
Status becomes something to sort by on demand, not the default.

## 5. Survey From / Survey To

Populated from `state.windowOpens` / `state.windowCloses` whenever the
certificate has an outstanding survey to show (`state.survey !=
SurveySeverity::NotRequired`), **regardless of how urgent it is** — so these
columns always tell you when the next survey falls due, not only once it's
close. Both blank when:
- the certificate needs no survey at all (`NotRequired` — an expiry-only
  certificate, e.g. Tonnage), or
- every survey track is satisfied and only the renewal remains (`nextSurveyType
  == SurveyType::Renewal`; `windowOpens`/`windowCloses` are unset in this case).

Check `QDate::isValid()` on the two fields to decide blank vs. populated — don't
infer from `SurveySeverity` alone, since that's a second read of the same fact.

## 6. Days Left

Always shown, as the raw signed `state.daysLeft` — a plain integer, negative once
overdue (e.g. `-7`). No special "(7 overdue)" formatting; the Status column's
colour and label already say that clearly, and a plain number is simpler to get
right than a second formatting rule.

One edge case: a certificate with no expiry date at all (`expiryDate` invalid,
"never expires") short-circuits inside `computeCertificateState()` and its
`daysLeft` is meaningless (defaults to `0`, which would misleadingly read as
"due today"). Show **blank**, not `0`, for this case — check
`certificate.expiryDate.isValid()` before deciding what to put in this cell,
the same way `expiryLabel()` already does for the `Expiry Date` column.

## 7. "Needs attention" filter

A single `QCheckBox` above the table, unchecked by default: **"Show only
certificates needing attention."** When checked, hide any row whose
`DisplayStatus == Valid`; this is an in-memory filter applied after computing
state for every row, not a SQL `WHERE` clause — severity is a derived value,
never stored (`CLAUDE.md` §6 rule 7), so there's nothing in the database to
filter on directly.

If the filter leaves zero rows for a vessel that does have certificates, show an
actual empty table — **not** the "select a vessel" prompt from step 5. That
prompt means "no vessel chosen"; a filtered-to-nothing table means something
different ("this vessel's certificates are all fine right now") and must not be
confused with it.

## 8. Where `computeCertificateState()` is called from

Inside `CertificateListWidget::reload()`, once the certificate list is loaded:
for each certificate, load its endorsements via the `EndorsementRepository`
already threaded through since step 6, then call

```cpp
computeCertificateState(certificate, endorsements, AlertThresholds(), QDate::currentDate());
```

`QDate::currentDate()` belongs exactly here and nowhere else in this call chain
— `computeCertificateState()` itself never reads the clock, by design (that's
what makes it testable), so the list screen is the one legitimate place "today"
gets read for this purpose.

This is one `EndorsementRepository::list()` call per certificate on every
reload — an N+1 pattern, not batched. Acceptable at the fleet sizes this app
targets; worth revisiting only if a real vessel's certificate count makes it
noticeably slow, not pre-optimized now.

## 9. Edge cases to test

1. A certificate with no expiry and no survey requirement: `Status` = Valid (no
   highlight), `Survey From`/`Survey To` blank, `Days Left` blank (not `0`).
2. A certificate requiring only an annual survey, well before its window opens:
   `Status` = Valid or Due Soon depending on how close the window's opening is;
   `Survey From`/`Survey To` show the upcoming window regardless.
3. A certificate with a missed annual survey: `Status` = Survey Overdue (red),
   `Days Left` negative (days since the window closed).
4. A certificate past its expiry date: `Status` = Expired (red), `Days Left`
   negative (days since expiry).
5. A certificate that is both close to expiry *and* has an overdue survey at
   once: the worse of the two wins per the existing `computeCertificateState()`
   rule (already covered by step 6's tests) — this screen just displays
   whichever `DisplayStatus` comes back, no new logic here.
6. Every survey track satisfied, only renewal remaining: `Survey From`/`Survey
   To` blank, `Status` reflects only the expiry-driven severity.
7. The "needs attention" filter applied to a vessel where every certificate is
   Valid: an empty table, not the "select a vessel" prompt.
8. Clicking the Status header once, then again: ascending shows Valid first;
   descending surfaces Expired/Survey Overdue/Critical first.
9. Default sort (no header clicked) is still `No.` ascending, unchanged from
   step 5.

## 10. What's explicitly not built this step

- No editable thresholds (step 8).
- No sidebar badge or daily toast (step 9).
- No filter beyond the single needs-attention checkbox — no filter by category
  or by specific status.
- No colour theming beyond the fixed palette above.
