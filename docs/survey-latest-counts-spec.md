# Annual surveys — the latest survey counts

Version 1.0. A change to how the annual track is evaluated, decided with the
developer after a real certificate showed the problem.

---

## 1. The problem, on a real certificate

V TRE, No. 12, Load Line Certificate: issued 29 May 2022, expires
09 Mar 2027, so its anniversaries are 09 Mar 2024, 2025 and 2026. One
annual endorsement recorded: 12 Apr 2026, Iskenderun.

The list showed it **Overdue, −519 days**, window 09 Dec 2024 – 09 Jun 2025.

Why: `SurveySchedule::evaluateAnnual()` walks the anniversaries in order and
gives each one the earliest endorsement dated on or after its window opened
(`certificate-control-spec.md` §4.5, "as amended"). The 2026 survey was
therefore claimed by the **2024** anniversary, counted as a late survey for
it, and 2025 was left with nothing. Only after the developer entered every
past annual date did each anniversary get its own and the warning clear.

That is the wrong question for how this office works. The company's S-14
report records the **last** annual survey, not the full history, and that
is what gets entered.

## 2. The rule, decided with the developer

**The latest annual survey covers its own anniversary and every anniversary
before it.** Only anniversaries after it are still to be done.

This is not a new idea. `certificate-control-spec.md` §3.3 already says:
"The system always looks *forward* from the latest endorsement of the
matching survey type." The claim-walk added to §4.5 for late surveys drifted
from that; this round puts the code back on §3.3.

**Accepted trade-off, stated plainly:** once a later annual survey is
recorded, a genuinely missed earlier one is no longer flagged. The developer
chose this knowingly — in practice a missed annual is dealt with by the
flag/class at the time, and the office tracks forward from the last survey.

**History is not touched.** Every endorsement stays in the `endorsement`
table and in the certificate's Endorsements list exactly as recorded. Only
the evaluation changes. No migration, no schema change.

This also settles the "endorsement model rework" question deferred since
step 13 (keep only the most recent survey per track): the answer is keep
the full history, schedule from the latest.

## 3. Algorithm — `SurveySchedule::evaluateAnnual()`

Candidates are built exactly as today: every `ANNUAL` endorsement, plus —
under `REPLACES_ANNUAL` only — an `INTERMEDIATE` endorsement that falls
inside the 2nd or 3rd annual window. Then:

```
latest = the most recent candidate (by endorsement_date), if any

if there is no latest:
    next requirement = anniversaries[0]            (unchanged behaviour)

else:
    covered = the LAST anniversary whose annualWindow opens on or before
              latest.endorsement_date
    if there is no such anniversary (latest is before the first window opens):
        next requirement = anniversaries[0]        (it covers nothing — unchanged)
    else:
        every anniversary up to and including `covered` is satisfied
        if latest.endorsement_date > annualWindow(covered).closes:
            lateNotes gets "The <covered> survey was completed late, on <date>."
        next requirement = the anniversary after `covered`
        if there is none: only the renewal remains (NotDue)
```

Severity, window and `outstanding` for the next requirement are computed
exactly as today (`severityFor()`, `annualWindow()`).

Checked against §1's certificate: windows open 09 Dec 2023, 09 Dec 2024,
09 Dec 2025. The last one opening on or before 12 Apr 2026 is 2026's →
2024, 2025 and 2026 are all satisfied, 12 Apr 2026 is inside 2026's window
(no late note), no anniversary remains → `NotDue`, only the renewal left.

**The intermediate track is unchanged.** It is one survey per term and
already counts any intermediate endorsement on or after its window opens.

## 4. Documentation

Already updated alongside this spec, not Claude Code's job:
`certificate-control-spec.md` §4.5 (the late-survey paragraph now points
here) and §4.8 item 6. Claude Code updates the comments in
`SurveySchedule.cpp` that describe the old claim-walk.

## 5. Tests — `tst_CertificateStateSurvey.cpp`

New:
- **The case from §1:** only the latest anniversary endorsed, `today` after
  an earlier anniversary's window closed → not `Overdue`; the next
  requirement is the anniversary after the endorsed one, or `NotDue` when
  none is left.
- **Late note only for the latest survey:** a single survey dated after its
  own window closed (but before the next window opens) satisfies that
  anniversary and every earlier one, and `reason` mentions "late" once, for
  that anniversary.
- **The accepted trade-off, pinned deliberately:** surveys at anniversaries
  1 and 3, none at 2 → anniversary 2 is **not** reported. The test's
  comment says this is the developer's decision, so nobody "fixes" it later.

Existing tests: most still hold as they are (each was written with
surveys in order). `eachAnniversaryClaimsTheEarliestEndorsementAvailableToIt`
still passes but its name now describes the old rule — rename it.
`anEndorsementBeforeTheFirstWindowOpensSatisfiesNothing` must keep passing
unchanged.

**Run the whole suite, not just this file.** Other suites
(`tst_CertificateListStatus`, `tst_CertificateCsvImportSurveys`,
`tst_CertificateRenewal`, `tst_Alerts`) may have expectations built on the
claim-walk. Any existing assertion that has to change must be listed in the
report with the reason — never edited silently to make it pass.

## 6. Not changed

- The `endorsement` table, its data, and the endorsement UI.
- The intermediate track, extensions, renewal dating, expiry severity.
- CSV import/export (it already carries whatever dates are recorded).
