# Certificate survey deadline fix

## 1. Purpose

A bug, not a new feature: `CertificateState::dueDate` — and therefore the
Days Left column, the Survey From/To columns' relationship to it, and
(since certificate-attachment-addendum-2-spec §7) the name an attached
file is given — currently points at the wrong date for a large part of a
survey's life.

Found by the developer manually testing the refreshed addendum-2 build: a
certificate whose annual survey window had not opened yet (today well
before Survey From) had its attached document named after the date the
window *opens*, a year or more before the survey could ever become
overdue. That date is not a deadline; it is a heads-up. The actual
deadline — the last day the survey can still be carried out before the
certificate goes invalid — is always when the window *closes*.

## 2. The bug, precisely

`src/modules/certificates/domain/CertificateState.cpp`, inside
`computeCertificateState()`'s outstanding-survey branch:

```cpp
state.dueDate  = (today < reported.window.opens) ? reported.window.opens
                                                   : reported.window.closes;
state.daysLeft = today.daysTo(state.dueDate);
```

Before the window opens, `dueDate` is the opening date. Once the window
is open (or overdue), it switches to the closing date. This is original
step 6 logic, unchanged since before this project had any attachment
feature at all. Addendum 2 did not introduce it — `nameDateFor()` just
started reading the same field for its own purpose, which is what
surfaced the problem.

## 3. The fix

Drop the switch. Once a survey is outstanding, `dueDate` is always the
window's close — full stop, regardless of whether today is before or
after the window opens:

```cpp
state.dueDate  = reported.window.closes;
state.daysLeft = today.daysTo(state.dueDate);
```

That is the entire production-code change in `CertificateState.cpp`. The
two branches around it are untouched and already correct:

- The active-extension branch already sets `dueDate = extensionExpiry`,
  which is already a real deadline.
- The nothing-outstanding branch already sets `dueDate =
  certificate.expiryDate`, also already a real deadline.

## 4. What does *not* change

- `SurveySeverity` (`NotDue` / `DueSoon` / `InWindow` / `Overdue`) is
  computed separately, in `SurveySchedule::evaluateAnnual()` /
  `evaluateIntermediate()`, from how close the window's **opening** is.
  That drives the list's colour/status badge and the "needs attention"
  filter, and none of it changes. A certificate still turns `DueSoon`
  the same number of days before its window opens as it always has —
  only the *number* in the Days Left column, and the date used for
  naming an attached file, changes for that row while it is in that
  state.
- `windowOpens` and `windowCloses` themselves are unchanged. Only which
  one `dueDate`/`daysLeft` point to is changing.
- The Expiry, Survey From and Survey To columns are unchanged — they
  already show the certificate's and the window's own dates directly,
  not anything derived through `dueDate`.
- A certificate with no survey requirement, and a certificate under an
  active short-term extension, are both unaffected — covered by the two
  branches in §3 that already point at a real deadline.

## 5. Visible consequence

Days Left will show a larger number for any row currently in `DueSoon`
or `NotDue` with a survey outstanding but its window not yet open — it
now counts all the way to the deadline instead of to the heads-up. For
the certificate that surfaced this (annual window 02 Aug 2027–02 Feb
2028, today 02 Nov 2026): Days Left changes from **273** (to the window
opening) to **~455** (to the window closing), and the attached file's
generated name changes from `..._2027-08-02...` to `..._2028-02-02...`
the next time something is attached to it. Nothing renames a file
already on disk — consistent with addendum 2 §7's "computed once, never
renamed" rule, which still applies unchanged.

## 6. Scope — files to touch

- `src/modules/certificates/domain/CertificateState.cpp` — the one-line
  fix in §3. `CertificateState.h`'s `dueDate` field and its doc comment
  need no change; the comment already just says "the date `daysLeft`
  counts to," which stays true.
- `tests/modules/certificates/tst_CertificateStateSurvey.cpp` (step 6's
  original suite) — almost certainly has assertions pinned to the old
  before/after-window-opens switch for the `DueSoon`/`NotDue` cases.
  **Read this file in full before changing anything in it** — do not
  find-and-replace blindly, since some of its assertions (severity,
  `windowOpens`, `windowCloses` themselves) are unrelated to this bug
  and must not move.
- `tests/modules/certificates/tst_CertificateAttachmentNaming.cpp` —
  two functions explicitly assert the old value:
  `theDueDateIsAlwaysTheDateDaysLeftCountsTo()` (its "a survey
  outstanding" case currently only asserts `dueDate < expiryDate`, which
  stays true either way — check whether it needs a sharper assertion
  now that the rule is simpler) and `theAnnualWindowWinsWhenItOpensFirst()`
  (currently asserts the stored filename against `windowOpens(firstAnniversary)`
  — this must change to `windowCloses(firstAnniversary)`). Check
  `theIntermediateWindowWinsWhenItIsTheOneDue()` too: it already expects
  `intermediateCloses` because today is already inside that window in
  that test, so it may need no change — confirm rather than assume.
- `tests/modules/certificates/tst_CertificateListStatus.cpp` and
  `tests/modules/certificates/tst_CertificateListWidget.cpp` — check
  both for any assertion on a specific Days Left number for a row whose
  survey window is not yet open. Not certain either one has one; worth
  a search rather than a guess.
- `docs/certificate-control-spec.md` §4.4 — currently documents the old
  before/after-window-opens rule for Days Left; needs correcting to
  describe the single always-counts-to-close rule.
- `docs/certificate-attachment-addendum-2-spec.md` §7 — currently says
  the filename carries "whichever of the expiry date, the next annual
  survey window, and the next intermediate survey window is soonest,"
  worded around the old opens-or-closes ambiguity. Needs a correction
  note (not a rewrite of the whole section) pointing at this fix,
  clarifying that "the next survey window" always means its *close*.

## 7. Out of scope

- No change to `SurveySchedule.cpp`'s window calculation itself
  (anniversary ± 3 months) — only `CertificateState.cpp` reads the
  result differently now.
- No change to alert severity, the sidebar badge, or the "needs
  attention" filter — all three already key off `SurveySeverity`, not
  `dueDate`/`daysLeft`.
- No change to any already-attached file's stored name or path. This is
  a go-forward naming correction only, exactly like addendum 2 §7 itself
  was.
