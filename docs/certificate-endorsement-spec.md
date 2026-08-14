# Endorsement Model & Certificate State — Specification

Version 1.0. This document defines *what step 6 does*. Domain rules for
endorsements and the state calculation live in
`docs/certificate-control-spec.md` §3–4; architecture rules live in
`CLAUDE.md`. This step is the "heart of the module" `certificate-control-spec.md`
§4 refers to — the first point where the software can say whether a
certificate is actually in good standing, not just record its paperwork.

---

## 1. Purpose

Record that a survey happened (an endorsement), and compute from that record
— plus the certificate's own dates and survey-rule fields — whether the
certificate is valid, due for a survey, or overdue. Nothing about status
colours or the days-left column on the list yet; that is step 7, once this
step's engine exists for it to read from.

## 2. Scope

In scope: the `endorsement` table, `EndorsementRepository`, the `Endorsement`
domain struct, `computeCertificateState()` and its supporting types
(`CertificateState`, `SurveyType`, `ExpirySeverity`, `SurveySeverity`,
`DisplayStatus`, `AlertThresholds`), and a minimal way to record an
endorsement against an existing certificate from the GUI.

Out of scope, deferred per `CLAUDE.md` §11: extensions and the `extension`
table (step 11 — see the note under `certificate-control-spec.md` §4's
function signature); the renewal workflow and what a `RENEWAL`-type
endorsement does (step 9); the editable `AlertThresholds`/`app_setting`
screen (step 8 — this step ships thresholds as hardcoded defaults, see §5
below); the certificate list actually showing colours, filters, or days-left
(step 7); editing or deleting an endorsement once recorded (see §6 — this
step is add-only).

## 3. Data model

```
endorsement
    id, certificate_id
    survey_type          -- 'INITIAL' | 'ANNUAL' | 'INTERMEDIATE' | 'RENEWAL'
    endorsement_date      -- required; a calendar date, not an instant
    place                 -- optional
    surveyor              -- optional
    result                -- optional, free text (e.g. "Satisfactory")
    remarks                -- optional, free text
```

Standard audit columns (`CLAUDE.md` §6.5) on top, as every table has. No
`previous_certificate_id`-style deferred column here — unlike `certificate`,
nothing about this table's shape depends on a later step; there is no reason
to hold anything back.

**Why `survey_type` is not optional**, even though the developer initially
only asked for date and place: `certificate-control-spec.md` §3.4 requires
matching an endorsement against a requirement of the *same* type — "an
annual endorsement never satisfies an intermediate requirement." Without
recording which one an endorsement was, `computeCertificateState()` has
nothing to match against. Only `ANNUAL` and `INTERMEDIATE` are chosen from
in this step's form (§6); `INITIAL` and `RENEWAL` are valid stored values
(a later step may write them) but are not offered as choices yet, and are
never read by the calculation (`certificate-control-spec.md` §4's note under
the function signature, and §4.8 item 14).

**No foreign key on `endorsement.certificate_id` → `certificate.id`**, for
the same reason `certificate.vessel_id` has none (`docs/PROJECT-STATUS.md`,
"Decisions confirmed"): nothing in this schema uses a `REFERENCES` clause
yet, and `EndorsementRepository` re-validates the certificate exists and is
in scope itself (§6). Revisit alongside that same decision, at the same
future step.

Migration: `migrations/005_create_endorsement.sql`. `004` is already
committed and stays untouched (`CLAUDE.md` §6.6).

## 4. Domain

```cpp
enum class SurveyType { Unset, Initial, Annual, Intermediate, Renewal };
```

`Unset` exists for the same reason `CertificateCategory::Unset` does
(`docs/PROJECT-STATUS.md` — approved for `Certificate` in step 5): the form
needs to represent "not chosen yet," and the repository needs to be able to
reject it. Never stored.

```cpp
struct Endorsement {
    QString     id;
    QString     certificateId;
    SurveyType  surveyType = SurveyType::Unset;
    QDate       endorsementDate;
    QString     place;
    QString     surveyor;
    QString     result;
    QString     remarks;
};
```

`certificate-control-spec.md` §4 already specifies `CertificateState`,
`computeCertificateState()`, and the enums it returns
(`ExpirySeverity`/`SurveySeverity`/`DisplayStatus`) — that section is this
step's implementation target, unchanged by this document. Two supporting
pieces it references are defined here, since nothing has needed them until
now:

```cpp
struct AlertThresholds {
    int criticalDays    = 30;  // certificate-control-spec.md §4.3
    int expiringSoonDays = 60;
    int dueSoonDays      = 90;
};
```

A plain struct with hardcoded defaults, per the note added to §4.3 — no
`app_setting` read happens in this step. Constructing a default-initialised
`AlertThresholds` is how every call site gets 30/60/90 until step 8 gives
them a way to override it.

`computeCertificateState()` lives in
`modules/certificates/domain/CertificateState.h/.cpp` — a new file, not
added to `Certificate.h`, because it is a free function operating *on* a
certificate plus its endorsements, not a member of the certificate's own
data. Splitting it out also keeps `Certificate.h` from growing past the
point a beginner can read in one sitting.

## 5. The late-endorsement rule (new since the design discussion)

`certificate-control-spec.md` §4.5's original pseudocode required an
endorsement to fall *within* a window to satisfy it, which — read literally
— means a certificate that ever missed a window would show `Overdue`
forever, even after being surveyed. That can't be right: a late survey is
still a survey. §4.5 has been amended: an endorsement satisfies the earliest
unsatisfied anniversary whose window it falls on-or-after the opening of, no
matter how long after that window's *close* it arrives. When it does arrive
late, the anniversary is resolved (no longer `Overdue`), but
`CertificateState.reason` records that it was late — visible in the same
tooltip/alert text everything else already flows through, not a new field on
the struct. See `certificate-control-spec.md` §4.5 and §4.8 item 6 for the
full wording.

## 6. Screens

No new sidebar entry, no new module — endorsements are recorded from inside
an existing certificate, since an endorsement without a certificate to
belong to means nothing.

**`CertificateEditDialog` gains an "Endorsements" section**, visible only
when editing an existing certificate (`m_isEditing`) and only when that
certificate requires at least one kind of survey
(`requiresAnnualSurvey || requiresIntermediateSurvey`). A brand-new
certificate being added has no id yet — nothing to attach an endorsement to
— and a certificate that needs no survey at all has nothing meaningful to
record. The section is a small table (Type, Date, Place) below the existing
form, populated from `EndorsementRepository::list(certificateId)`, plus an
"Add Endorsement" button.

**`EndorsementEditDialog`/`EndorsementEditForm`** — the same
form-wrapped-in-dialog shape every other add/edit screen in this project
uses. The survey-type combo only offers the types this specific certificate
actually requires: `ANNUAL` if `requiresAnnualSurvey`, `INTERMEDIATE` if
`requiresIntermediateSurvey`, both if both are set. If only one is required,
skip the combo and set the type directly — asking someone to choose between
one option is friction, not a decision.

**Add-only for this step** — no editing or deleting a recorded endorsement.
An endorsement is a compliance record; getting edit/delete right (should an
edit be a new revision? should a delete be possible at all, or only a
correction entry?) is a real design question, not a small one, and nothing
in step 6 depends on the answer. Revisit if it becomes a real need rather
than guessing now.

## 7. Repository behaviour

`EndorsementRepository` mirrors `CertificateRepository`, with one structural
difference: `endorsement` has no `vessel_id` of its own, so scope is checked
transitively through the certificate it belongs to.

- `list(certificateId, ...)` joins against `certificate` and applies the
  same VESSEL-mode scope filter used everywhere else — a request for
  another vessel's certificate's endorsements returns nothing, the
  established "not found is a successful answer" precedent, exactly as
  `CertificateRepository::list()` already behaves.
- `create()` looks up the parent certificate first (one query: its
  `vessel_id` and `issue_date`) — refusing outright, with a friendly
  message, when the certificate doesn't exist, is deleted, or (in VESSEL
  mode) isn't the installation's own. That same lookup's `issue_date` feeds
  the "endorsement before issue date" check (§4.8 item 5) without a second
  query.
- `validate()` rejects: `surveyType == Unset`, a missing `endorsementDate`,
  and an `endorsementDate` before the certificate's `issueDate` — all with
  friendly messages, the raw `CHECK` constraint (`survey_type` in the four
  known codes) staying the backstop behind them, same shape as every
  repository so far.
- No `update()`/`delete()` this step (§6).

## 8. Edge cases that must have unit tests

Mirrors `certificate-control-spec.md` §4.8, plus the repository- and
UI-layer cases specific to this step:

1–14. Every item in `certificate-control-spec.md` §4.8, tested directly
against `computeCertificateState()` as a pure function — no database, no
widgets, per `CLAUDE.md` §4 rule 1.

15. `EndorsementRepository::create()` refuses an endorsement for a
    certificate that doesn't exist, is soft-deleted, or (VESSEL mode)
    belongs to another vessel.
16. `EndorsementRepository::create()` refuses `surveyType == Unset`, a
    missing date, and a date before the certificate's issue date — each
    with a friendly message, and separately, a test confirming the raw
    `CHECK` constraint text never reaches the user.
17. `EndorsementRepository::list()` never returns another vessel's
    certificate's endorsements under VESSEL mode, even one seeded directly.
18. The "Endorsements" section is hidden on a brand-new (unsaved)
    certificate, and on a certificate requiring no survey at all.
19. The survey-type combo offers only `ANNUAL` when only the annual survey
    is required, only `INTERMEDIATE` when only that one is, and both when
    both are — never `INITIAL`/`RENEWAL`.

---

Once this is built and verified, step 7 wires `computeCertificateState()`
into `CertificateListWidget` — the status colours, the days-left column, and
filtering by status — using the thresholds' hardcoded defaults from §4 above
until step 8 makes them editable.
