# Anniversary dates — fix for certificates a few days short of whole years

Version 1.0. A safety bug in Certificate Control (tagged
`certificate-control-v1.0`), found by the developer on screen. Fix ships as
`certificate-control-v1.0.1`.

---

## 1. What the developer saw

V TRE, No. 14, S.M.C. — issued **12 Jan 2026**, expires **30 Dec 2030**,
intermediate survey only. The list showed the intermediate window as
**30 Sep 2028 – 30 Mar 2030**. Correct is **30 Sep 2027 – 30 Mar 2029** —
the program is a full year late.

Same cause, worse effect: No. 13, DOC (Office ISM) Annual — issued
**16 Jan 2026**, expires **21 Dec 2030**. The list shows the next annual
window as 21 Sep 2027 – 21 Mar 2028, "494 days". The real first annual
window is **21 Sep 2026 – 21 Mar 2027** — open now. A due survey is shown
as Valid.

## 2. Cause

`SurveySchedule.cpp`:

```cpp
int termInYears(const Certificate& certificate)   // whole years, rounded down
...
for (int n = termInYears(certificate) - 1; n >= 1; --n)
    anniversaries.append(certificate.expiryDate.addYears(-n));
```

12 Jan 2026 → 30 Dec 2030 is two weeks short of five years, so the term
counts as **4**, and the list is `expiry − 3, −2, −1` years = 2027, 2028,
2029. **30 Dec 2026 is missing.** Every anniversary index is then off by
one: `anniversaries.at(1)` and `.at(2)`, meant to be the 2nd and 3rd
anniversaries, are really the 3rd and 4th.

The code follows `certificate-control-spec.md` §4.5 exactly — the spec
itself stated the rule as whole years. The spec was wrong.

## 3. The correct rule

SOLAS / HSSC: the **anniversary date** is the day and month of the
certificate's expiry date, in each year of its term.

The term must be counted **to the nearest whole year**, not rounded down:

> `term` = the time from issue date to expiry date, in whole years,
> **rounded to the nearest year** (six months or more rounds up).
> Anniversaries = expiry date minus *n* years, for *n* = `term − 1` down
> to 1.

Why rounding, not "every date with that day and month after the issue
date": the issue date and the expiry day/month differ by up to about three
months in both directions in normal practice —
- **renewal done within 3 months before expiry:** the new certificate runs
  from the old expiry date, so it is issued *before* it starts — e.g. issue
  1 Jun 2025, expiry 21 Jun 2030. 21 Jun 2025 is the start of the new
  term, not an anniversary needing a survey;
- **issued after the day/month** (renewal after an extension, a late
  issue): e.g. issue 12 Jan 2026, expiry 30 Dec 2030. 30 Dec 2026 *is* the
  first anniversary.

Rounding to the nearest year gets both right; rounding down (today) misses
the first anniversary of the second kind; "anything after the issue date"
would invent one for the first kind.

Day that doesn't exist in that year (29 Feb) → last day of that month, as
today (`QDate::addYears` already clamps).

For the S.M.C. (4 years 11½ months → 5): 30 Dec 2026, 2027, 2028, 2029.
The 2nd and 3rd are 2027 and 2028; intermediate window 30 Sep 2027 –
30 Mar 2029. For the DOC (4 years 11 months → 5): first anniversary
21 Dec 2026.

## 4. Change

- `SurveySchedule`'s `termInYears()`: round to the nearest whole year as
  in §3 instead of rounding down. `anniversariesOf()` keeps its shape.
  Check what else calls `termInYears()` and report it.
- Everything downstream (`evaluateAnnual`, `evaluateIntermediate`,
  `REPLACES_ANNUAL`, attachment due-date naming, alerts) uses the list as
  it is; no other change should be needed. Report if anything else
  assumed the old list.
- `certificate-control-spec.md` §4.5: replace the line

  ```
  anniversaries = [ expiryDate minus n years  for n = totalYears-1 down to 1 ]
  ```

  with

  ```
  totalYears    = issue → expiry in years, rounded to the NEAREST year
  anniversaries = [ expiryDate minus n years  for n = totalYears-1 down to 1 ]
  ```

  and add one sentence under it: "Corrected by
  `docs/anniversary-dates-fix-spec.md` — counting whole years rounded
  down dropped the first anniversary of any certificate a few days short
  of whole years."

## 5. Tests — `tst_CertificateStateSurvey` / `…Matching`

- **The S.M.C. case, real dates:** issue 2026-01-12, expiry 2030-12-30 →
  anniversaries 2026-12-30, 2027-12-30, 2028-12-30, 2029-12-30;
  intermediate window 2027-09-30 – 2029-03-30.
- **The DOC case, real dates:** issue 2026-01-16, expiry 2030-12-21, no
  endorsements, today 2026-11-13 → next annual is 2026-12-21, window
  2026-09-21 – 2027-03-21, **in window** (not Valid).
- An exact five-year certificate (issue 2025-06-21, expiry 2030-06-21) still
  has four anniversaries, 2026–2029 — unchanged.
- **Renewal within 3 months before expiry** (issue 2025-06-01, expiry
  2030-06-21) — four anniversaries, 2026–2029; **2025-06-21 is not one**.
- **Issued after an extension** (issue 2026-03-20, expiry 2030-12-30, about
  4 years 9 months → 5) — four anniversaries, 2026-12-30 first.
- Rounding boundary: 4 years 6 months exactly rounds up to 5; 4 years 5
  months rounds down to 4.
- 29 Feb expiry; a short-term certificate (under 18 months) → no
  anniversaries, as today.
- Prove the S.M.C. and DOC tests **fail on the old code** first.

**Run the full suite.** Existing tests built on whole-year certificates
should pass unchanged. Any existing assertion that changes is listed with
its reason, never edited silently.

## 6. Ship it

`PROJECT-STATUS.md` entry (a bug in a closed module, found on screen, and
that the spec was the origin). Clean Debug + Release rebuild, redeploy
`dist\` (exe first, then `windeployqt`; close the app first). Commit, tag
**`certificate-control-v1.0.1`** (annotated), push commit and tag.
