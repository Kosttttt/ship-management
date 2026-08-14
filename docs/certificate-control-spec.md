# Certificate Control — Module Specification

Version 2.0. This document defines *what the module does*. Architecture rules
live in `CLAUDE.md` at the repository root.

---

## 1. Purpose

Every ship must carry official certificates proving it is safe, legally
compliant and properly inspected. Certificates expire, and most require
periodic surveys before they expire. A missed survey or a lapsed certificate
can lead to detention by Port State Control.

The module keeps track of every certificate, warns when action is needed, and
ensures nothing slips through.

## 2. Scope

**A certificate belongs to a vessel.** That is the only relationship in this
module.

- Statutory certificates: Safety Construction, Safety Equipment, Safety Radio,
  Load Line, IOPP, IAPP, ISPP, Sewage, Garbage, Tonnage, ISSC, SMC, DOC (the
  company copy carried on board), MLC, AFS, BWM, and others.
- Class certificates: hull, machinery, docking, tailshaft, boilers.
- Certificates covering an item of equipment (lifeboat, liferaft, EPIRB,
  lifting gear, fire extinguishers) are entered as **ordinary certificates**,
  with a free-text `applies_to` field describing the item — for example
  *"Liferaft No. 3, S/N 44821"*.

**There is no equipment registry.** Equipment and planned maintenance belong to
a future PMS module. This module does not read from, write to, or depend on any
equipment table.

**Crew (STCW) certificates are out of scope** — a future crew module.

All certificate data is entered manually through the add/edit form, or in bulk
through CSV import.

---

## 3. Core concepts

### 3.1 Certificate dates

Every certificate has:

- an **issue date** — when it was granted
- an **expiry date** — when it runs out (normally 5 years after issue)
- **anniversary dates** — the same day and month as the expiry, in each
  intervening year

Example — a certificate expiring **04 February 2028**:

| Anniversary | Date |
|---|---|
| 1 | 04 Feb 2025 |
| 2 | 04 Feb 2026 |
| 3 | 04 Feb 2027 |
| Expiry | 04 Feb 2028 |

### 3.2 Survey windows

A survey need not fall exactly on the anniversary. It may be carried out within
a window of **3 months before to 3 months after** the anniversary date.

```
Anniversary:    04 February 2026
Window opens:   04 November 2025
Window closes:  04 May 2026
```

If the window closes without the survey being done, **the certificate becomes
invalid**, even though its expiry date has not been reached. See §4.1.

### 3.3 The endorsement date

When a survey is completed the certificate is **endorsed** — a stamp confirming
the survey took place. The endorsement date is the most important field in the
module. It tells the system:

> "The last survey of this type was done on this date. The next one is due at
> the following anniversary."

The system always looks *forward* from the latest endorsement **of the matching
survey type**.

### 3.4 Survey types — annual and intermediate run in parallel

**Annual survey** — required at **every** anniversary date.
Window: anniversary − 3 months to anniversary + 3 months.

**Intermediate survey** — required **once** in the life of the certificate, with
its own wider window and a **different scope of survey**.
Window: 2nd anniversary − 3 months to 3rd anniversary + 3 months.

These are **two independent tracks**. The annual survey is still required at
the 2nd and 3rd anniversaries; the intermediate covers additional items on top.

*Basis: IMO HSSC. For the Cargo Ship Safety Construction Certificate, the
intermediate survey is completed within three months of either the second or
the third anniversary date, and the items additional to the annual survey scope
may alternatively be carried out at the second or third annual survey, or
between them.*

**Priority.** When an annual window and the intermediate window are both open,
the **annual takes priority** — its window closes sooner.

**Matching.** An annual endorsement never satisfies an intermediate
requirement, and an intermediate endorsement never satisfies an annual. The
scopes differ. Each endorsement records its own `survey_type` and is matched
only against a requirement of the same type.

**Per-certificate exception.** A small number of certificates substitute
rather than add — the periodical survey for the Cargo Ship Safety Equipment
Certificate takes the place of one of the annual surveys. There is no shared
certificate type record to hold this (§5) — different vessels can legitimately
carry what looks like "the same" certificate under different terms (short-term
vs. full-term, differing authorities), so nothing is looked up or inherited.
Whoever enters the certificate sets this directly on the record:

```
intermediate_mode = 'ADDITIONAL'       -- parallel tracks, as above — the common case
                  | 'REPLACES_ANNUAL'  -- set only where the rule says so
```

**Renewal survey** — carried out before expiry to issue the next certificate.

---

## 4. The state calculation

The heart of the module. It lives in the **domain layer** as a pure function,
with no database and no widgets:

```cpp
struct CertificateState {
    ExpirySeverity  expiry;        // Valid, ExpiringSoon, Critical, Expired
    SurveySeverity  survey;        // NotDue, DueSoon, InWindow, Overdue, NotRequired
    DisplayStatus   display;       // the more severe of the two — what the grid shows
    bool            isValid;       // false if expired OR any survey overdue
    SurveyType      nextSurveyType;
    QDate           nextAnniversary;
    QDate           windowOpens;
    QDate           windowCloses;
    int             daysLeft;      // see §4.4
    QString         reason;        // human-readable, used in alerts and tooltips
};

CertificateState computeCertificateState(const Certificate& cert,
                                         const QList<Endorsement>& endorsements,
                                         const AlertThresholds& thresholds,
                                         QDate today);
```

`today` is a **parameter, never `QDate::currentDate()` inside the function**.
That is what makes every rule below unit-testable.

There is no `CertificateType` parameter — no such type exists (§5). Everything
this function used to read from it (`requiresAnnualSurvey`, `requiresIntermediateSurvey`,
`intermediateMode`) is a field on `cert` itself, set by whoever entered that
certificate.

**No expiry.** `cert.expiryDate` may be null — some certificates (a Tonnage
Certificate, typically) never expire and have no periodic survey requirement
at all. When it is null, `expiry` is always `Valid`, `survey` is always
`NotRequired`, and the function returns immediately without touching §4.5's
anniversary calculation — that calculation counts backward from an expiry
date, so it has nothing to anchor to without one. A certificate entered with
`expiryDate` null and `requiresAnnualSurvey`/`requiresIntermediateSurvey` true
is invalid input (§4.8 item 13): a no-expiry certificate cannot have a survey
schedule, because there is no anniversary to schedule it against.

**No `extensions` parameter yet.** §4.7 describes extensions and §4.8 item 12
originally called for a test of one affecting this calculation, but there is
nowhere for that data to come from — no `extension` table exists, and the
signature above has no parameter for it. Extensions are deferred to their own
step (`CLAUDE.md` §11), slotted after the renewal workflow since both are
about a certificate's life running past its normal schedule. Until then, this
function has no notion of an extension at all; item 12 is deferred with it.

**Which survey types this function reasons about.** `survey_type` on an
endorsement has four possible values (§5), but only `ANNUAL` and
`INTERMEDIATE` are matched against a window by §4.5 below. `INITIAL` and
`RENEWAL` endorsements may exist for historical record-keeping, but this
function does not look for them and they satisfy nothing — a renewal
endorsement's effect is creating the *next* certificate row (step 9), not
changing this certificate's state.

### 4.1 An overdue survey invalidates the certificate

If any required survey window has closed without a matching endorsement, the
certificate is **not valid**, regardless of its expiry date. `isValid` is
false, and the display status is red — the same severity tier as Expired.

Expiry and survey are computed as two separate values purely so that the alert
can state *which* of the two is wrong: book a surveyor, or start a renewal. The
user sees one red row either way.

### 4.2 Both tracks are evaluated

Annual and intermediate are evaluated independently. The certificate's survey
severity is the **worst** of the two. `nextSurveyType` names the one that needs
attention first — the annual, when both are open.

### 4.3 Status table

Severity order, worst first:

| Colour | Status | Condition |
|---|---|---|
| 🔴 Red | **Expired** | `today > expiryDate` |
| 🔴 Red | **Survey Overdue** | a survey window closed with no matching endorsement — certificate invalid |
| 🔴 Red | **Critical** | expires within 30 days |
| 🟠 Orange | **Expiring Soon** | expires within 60 days |
| 🟡 Yellow | **Survey Due** | `today` is inside an open survey window |
| ⬜ Light | **Due Soon** | a survey window opens within 90 days |
| ⬜ White | **Valid** | none of the above |

The thresholds 30 / 60 / 90 are read from an `AlertThresholds` value passed
into the function — never a constant inside it, so a test can pass different
numbers. Until the Settings step (`CLAUDE.md` §11) builds the `app_setting`
table and a screen to edit them, `AlertThresholds` is constructed with these
three values hardcoded as defaults. The function itself never changes when
that step lands — only where the values it's handed come from.

Status is **computed on demand from today's date and never stored in the
database** (`CLAUDE.md` §6.7).

### 4.4 The "Days Left" column

Always shows the most actionable number:

- survey window **not yet open** → days until the window **opens** (plan ahead)
- survey window **open** → days until the window **closes** (how urgent)
- no survey required → days until **expiry**

### 4.5 Determining the next required survey

```
anniversaries = [ expiryDate minus n years  for n = totalYears-1 down to 1 ]
    (day and month of expiry; if that day does not exist in the target month,
     use the last day of that month)

annualWindow(n)     = [ anniversary[n] - 3 months , anniversary[n] + 3 months ]
intermediateWindow  = [ anniversary[2] - 3 months , anniversary[3] + 3 months ]

ANNUAL track:
    for each anniversary n in order:
        if no endorsement of type ANNUAL falls within annualWindow(n):
            that is the next annual requirement
            overdue if today > end of annualWindow(n)

INTERMEDIATE track (only if the type requires it):
    if no endorsement of type INTERMEDIATE exists:
        the requirement is open
        overdue if today > end of intermediateWindow

When intermediate_mode = 'REPLACES_ANNUAL', an INTERMEDIATE endorsement
inside annualWindow(2) or annualWindow(3) also satisfies that annual.
```

**A survey completed after its window closed still satisfies that
anniversary.** The pseudocode above says an endorsement must fall *within*
`annualWindow(n)`, but that leaves no way for a genuinely late survey to ever
resolve — read literally, a certificate that missed one window would show
`Overdue` forever, even after being surveyed, which does not match what
actually happens: a late survey is still a survey. The matching rule is
therefore: an endorsement of the matching type satisfies anniversary `n` if
its `endorsement_date` is on or after `annualWindow(n)`'s opening date and
has not already been claimed by an earlier, unsatisfied anniversary. There is
no upper bound at the window's close — only being superseded by an even
earlier open anniversary claiming it first. When such an endorsement's date
falls after `windowCloses`, the anniversary is satisfied (the certificate is
no longer `Overdue` for it), but `CertificateState.reason` records that it
was completed late — this is surfaced through the existing `reason` string,
not a new field. See §4.8 item 6.

Once every anniversary is satisfied, the next required action is the **renewal
survey** before expiry.

### 4.6 Renewal dating rule

- Renewal survey completed **within 3 months before expiry** → the new
  certificate runs 5 years from the **previous expiry date** (no time is lost).
- Renewal survey completed **more than 3 months before expiry** → the new
  certificate runs 5 years from the **survey completion date**.

### 4.7 Extensions

A certificate may receive a short extension (typically up to 3 months, or a
1-month grace to reach a port of survey). Extensions are recorded explicitly as
a dated, reasoned entry with the granting authority. They are **never applied
silently** by the calculation.

### 4.8 Edge cases that must have unit tests

1. Expiry on 29 February in a leap year; anniversaries in non-leap years.
2. Expiry on the 31st; a window boundary landing in a 30-day month.
3. Validity other than 5 years (interim, short-term certificates).
4. No endorsements yet (freshly issued certificate).
5. An endorsement dated before the issue date — reject as invalid input.
6. An endorsement completed after its window closed still satisfies that
   anniversary (the certificate returns to valid), and `reason` records that
   it was late — see the matching rule added to §4.5.
7. Annual overdue while the intermediate is still in window, and the reverse.
8. Both annual and intermediate windows open at once — annual is reported.
9. Expiring soon **and** survey overdue at the same time.
10. `today` exactly on a window boundary — boundaries are **inclusive**.
11. `intermediate_mode = 'REPLACES_ANNUAL'` — an intermediate endorsement
    satisfies the 2nd or 3rd annual.
12. **Deferred to the extensions step** (`CLAUDE.md` §11) — no `extension`
    table or parameter exists yet; see the note under §4's signature.
13. `expiryDate` null: `computeCertificateState()` returns `Valid`/`NotRequired`
    immediately. `expiryDate` null with either survey flag true is rejected as
    invalid input before it reaches the calculation at all.
14. An `INITIAL` or `RENEWAL` endorsement is present alongside `ANNUAL`/
    `INTERMEDIATE` ones — it is stored and returned by the repository, but
    ignored by the matching logic; it satisfies nothing and changes nothing.

---

## 5. Data model (indicative)

All tables carry the standard audit and sync columns from `CLAUDE.md` §6.5.

**There is no `certificate_type` table.** Earlier drafts of this spec had one
— a shared catalogue a certificate would look up its survey rules from. It was
dropped: different vessels can legitimately need different rules for what
looks like "the same" certificate (a short-term certificate on one ship, a
full-term one on another, issued by different authorities), so nothing about
a certificate's rules is safe to assume is shared or reusable. Building and
maintaining an accurate seeded catalogue against the IMO Compendium also isn't
this project's job to get right on the developer's behalf — every certificate
carries its own rules, entered by whoever adds it:

```
certificate
    vessel_id, certificate_number,
    name,                              -- free text, e.g. "Safety Construction Certificate"
    category ('STATUTORY' | 'CLASS' | 'EQUIPMENT' | 'OTHER'),  -- a broad bucket for
                                       -- filtering/reporting, not authoritative data
    applies_to,                       -- free text, e.g. "Liferaft No. 3"
    issue_date, expiry_date,          -- expiry_date is nullable: null means "does not expire"
    issued_by, place_of_issue,
    is_interim, previous_certificate_id, notes,
    requires_annual_survey, requires_intermediate_survey,     -- set per certificate,
    intermediate_mode ('ADDITIONAL' | 'REPLACES_ANNUAL')      -- not looked up (§3.4)

endorsement
    certificate_id,
    survey_type ('INITIAL' | 'ANNUAL' | 'INTERMEDIATE' | 'RENEWAL'),
    endorsement_date, surveyor, place, result, remarks

extension
    certificate_id, granted_until, granted_by, reason

attachment
    certificate_id, original_filename, relative_path, sha256,
    is_archived, archived_at
```

---

## 6. File attachments

Each certificate may have a scanned PDF attached, stored in a managed folder
outside the database.

```
Certificates/
  └── 9123456 - MV Example/
        ├── 1_Class Certificate_04-02-2028.pdf
        └── Archive/
              └── 1_Class Certificate_20-04-2027_archived_2026-04-28.pdf
```

- The vessel folder is keyed on **IMO number first**, with the name appended
  for human navigation. *Rationale: ships are renamed and sold; the IMO number
  is fixed to the hull for life. Naming folders after the ship would orphan
  every file on the first rename.*
- Filename pattern: `{sequence}_{certificate name}_{expiry dd-MM-yyyy}.pdf`
- **Uploading a replacement never deletes the previous file.** The old file is
  moved to `Archive/` with `_archived_{yyyy-MM-dd}` appended.
- The database stores the **relative** path plus a SHA-256 hash. The hash
  detects corruption or truncation after the folder is copied between ship and
  office.

---

## 7. Alerts and notifications

**Sidebar badge** — a red count on the Certificate Control button showing how
many certificates need attention. The tooltip breaks the number down by status.
Clicking it opens the certificate list already filtered to those certificates.

**Daily toast** — once per day, on first launch, a notification in the
bottom-right corner listing certificates requiring action. "Once per day" is
tracked by storing the last-shown date in `app_setting`; it is *not* a timer.

The badge count comes from the same `computeCertificateState()` function the
grid uses. There is exactly one implementation of the rules.

---

## 8. Import and export

**Purpose:** initial data entry. Loading several hundred existing certificates
is far faster in a spreadsheet than one dialog at a time.

- Export a certificate list per vessel; fill it in offline; import it back.
- **CSV first.** It opens directly in Excel and needs no third-party library.
  True `.xlsx` (via QXlsx) is added later only if column formatting proves
  necessary.
- On import, rows matching an existing certificate are **highlighted**, and the
  user chooses per row: skip or overwrite.
- Import runs inside a single database transaction: it either fully succeeds or
  changes nothing. A half-imported file is worse than a failed import.
- A dry-run preview is shown before anything is written.

---

## 9. Reporting

- Fleet or single-vessel compliance report
- Certificates expiring within a selectable period
- Overdue surveys
- Full history for one certificate, including endorsements and extensions

All reports must be **print-friendly** and exportable to PDF via
`QTextDocument` + `QPrinter`.

---

## 10. Visibility

Handled by installation mode (`CLAUDE.md` §3), not by roles:

- **Office installation** — all vessels in the fleet, ship selector shown
- **Vessel installation** — its own vessel only, ship selector hidden

Administrator / Office / Crew roles are layered on at the very end of the
project and add permissions (who may edit, delete, approve) on top of this
visibility, which already works.
