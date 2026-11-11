# Roadmap and pending items

A single-page "where are we" view, distilled from `CLAUDE.md` §11's build
order and `docs/PROJECT-STATUS.md`'s running log. Those two files remain the
full detailed record — this is the short version to orient a new session (or
yourself) quickly, not a replacement for either.

## Closing out Certificate Control — the plan

Decided with the developer: finish every pending item for this module, then
start the next one. Rounds, one at a time, in this order:

1. ✅ **Annual surveys: the latest survey counts.** `6861d24`, spec
   `docs/survey-latest-counts-spec.md`. The V TRE Load Line no longer shows
   overdue. Also settled the long-deferred "endorsement model rework"
   question: keep the full history, schedule from the latest.
2. **Housekeeping.** Spec written: `docs/housekeeping-round-spec.md`.
   `VesselRepository.cpp` and `CertificateListWidget.cpp` back toward
   ~300 lines using the new §9 helper rule; two test files split; the
   attachment chooser remembers the last folder; `TransactionScope`
   commit fix; `QSignalBlocker` include. The first-run duplicate-IMO item
   was dropped — it can't happen (§7 of that spec).
3. **Step 15, reduced: database backup & restore.** Spec to be written.
   Export database / Import database in Settings — a safe, in-app way to
   move to another computer or keep a backup (e.g. on Google Drive as a
   *file*, never as the live database). The audit trail viewer is
   deferred, not part of this module.
4. **Close-out.** Your own checks (below), a final `PROJECT-STATUS.md`
   entry marking the module complete, and a git tag on that commit so
   there is a named version to go back to.

Then **the next module** — Document Management is next on the project's
list — starting, as every step has, with a design discussion before any
spec.

## Completed

**Build order steps 1–13 ✅:** toolchain; core infra; first-run wizard;
vessel CRUD; certificate CRUD; endorsements + `computeCertificateState()`;
certificate list screen; settings; alerts; renewal; short-term
extensions; attachments; CSV import/export + one-vessel PDF.

**Rounds outside the numbered steps ✅:** certificate-list usability fixes;
List Number prefixes for the S-14 scheme (migration 011); vessel Archive /
Restore (`11efcd9`) and its follow-up — clearer IMO message,
`VesselArchiveActions` (`a2d17fe`).

## Dropped or deferred (decided, not forgotten)

- **Step 14, Reports** — dropped by the developer. The one-vessel PDF from
  step 13 is enough for this module.
- **Audit trail viewer** — deferred out of step 15.
- **`--data-dir` startup argument** — would let a manual check run against
  a throwaway database (`CLAUDE.md` §10.1). Not needed by step 15's design
  if backup/restore works from inside the running app; revisit if the
  step 15 spec says otherwise.
- **Ship-to-shore sync** and **role-based access control** — cross-module,
  deliberately later, per `CLAUDE.md` §11.
- **Database in a cloud-synced folder** — rejected. SQLite edited inside a
  folder that a sync client is copying, or open on two machines at once,
  ends in corruption or a "conflicted copy." Cloud storage is for an
  *exported backup file* only.

## Decisions made, for the record

- **Annual survey rule:** the latest annual survey counts for every
  anniversary before it. Trade-off accepted: a missed earlier annual is no
  longer flagged once a later one is recorded.
- **`CLAUDE.md` §9:** a class that outgrows ~300 lines hands self-contained
  pieces to small helper classes in the same layer, instead of being
  exempted or split across files.
- **Step 14 dropped; step 15 = database backup & restore only.**

## Small UX improvement, scheduled in round 2

**The attachment chooser remembers the last folder used**, instead of
always opening at Documents. `AttachmentChooser::startingFolder()` already
has a comment anticipating it: *"Documents rather than the last-used
folder, which nothing stores yet."* Store it in `QSettings`, like
`TableHeaderSettings` does for column widths — a per-machine UI
preference, not fleet data. Carry over the existing safety reason: the
remembered folder must never be the vessel's own `attachment_root_path`
(copying a file onto itself); fall back to Documents if it would be.

## Technical debt not covered by the rounds above

- **`%APPDATA%` vs. the virtualized path** the running app actually uses
  is avoided, not understood. Step 15 should work through the app's own
  open database connection rather than guess a path on disk.
- Environment quirks, low priority: no SQLite viewer installed (DB Browser
  for SQLite suggested); Qt Creator's F5 doesn't stop on breakpoints
  (Ctrl+R works).

## Your pending items

- **Push:** the archive rounds are pushed (up to `a2d17fe`). Not yet
  pushed: `71e76f3` (docs) and `6861d24` (latest-survey rule).
- **Check the V TRE Load Line (No. 12) on screen** — it should no longer
  show overdue.
- **V TRE CSV — two rows still blocked.** No. 19 (Safety Construction) and
  No. 39 (Ballast Water Management): each annual-survey date is before the
  certificate's current issue date, which the app refuses. Needs either the
  correct survey date or the correct issue date. After round 1 you only
  need the latest annual date for each, not the full history. No. 19's
  ~2-month issue-to-expiry span is also worth checking against the S-14
  sheet.
- **V ALDO CSV — clean.** 214 rows validated; ready to import.
- **Work laptop.** Your plan was to run `dist/migration-proof-kit/` against
  its real database before migration 011 reaches it. Still open — or, once
  step 15 exists, simply move to it with Export/Import instead.
