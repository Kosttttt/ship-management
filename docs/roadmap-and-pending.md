# Roadmap and pending items

A single-page "where are we" view, distilled from `CLAUDE.md` §11's build
order and `docs/PROJECT-STATUS.md`'s running log. Those two files remain the
full detailed record — this is the short version to orient a new session (or
yourself) quickly, not a replacement for either.

## Build order — completed vs. remaining

**Completed (✅ steps 1–13):**

1. Toolchain verified
2. Core infra — database, migration runner, logger
3. First-run wizard — installation mode + vessel identity
4. Vessel CRUD
5. Certificate CRUD
6. Endorsement model + `computeCertificateState()`
7. Certificate list screen — status colours, filters, days-left
8. Settings & `app_setting` — editable alert thresholds
9. Alerts — sidebar badge, daily toast, filtered drill-down
10. Renewal workflow
11. Extensions (short-term)
12. Attachments, with archive-on-replace
13. CSV import/export, plus a cut-down PDF export (one vessel, current
    state, no history, no fleet view)

Plus two feature rounds that sit on top of step 13's work rather than being
numbered build-order steps: the certificate-list usability fixes
(resizable/persisted columns, multi-select delete, placeholder issue dates),
and the certificate List Number format widening for the company's full S-14
prefix scheme (`S1`, `P.6`, `EU.9`, …), shipped via migration 011.

**Remaining, in order:**

14. **Reports.** Not started — no implementation spec written, no design
    discussion held yet. Needs deciding first: a fleet-wide version of
    step 13's statement (OFFICE mode only), a print preview before export,
    per-certificate survey history (every endorsement, not just current
    status), some combination, or deferring parts of it until a concrete
    need shows up. Step 13 already built the reusable pieces this will
    lean on (QTextDocument/QPrinter plumbing, PDF page setup, the
    status-label logic shared between screen and document), so whatever
    step 14 needs is narrower than it would have been before step 13
    existed.
15. **Audit trail viewer + backup/restore.** Not started. This is also
    where the "move data to another computer" problem would eventually get
    a real, built-in answer instead of the manual copy process described
    separately — see the machine-setup instructions.

**After the numbered steps:**

- **Ship-to-shore sync** — not started, no design work done at all yet.
- **Role-based access control** — deliberately last, by the project's own
  rule; not started. Note that "crew see only their own ship" is already
  delivered today by VESSEL installation mode, so that part isn't a role
  concern when this is eventually tackled.
- **Future modules**, each added one at a time only once Certificate
  Control (and presumably the steps above) are solid: Document Management,
  Planned Maintenance System, Spare Parts Inventory, Purchasing, Vessel
  Life Monitoring, Budget Control.

## Design decisions raised but deliberately not yet made

- **Endorsement model rework.** Raised while designing the CSV import's
  survey columns in step 13: whether `endorsement` and `SurveySchedule`'s
  matching logic should be reworked to keep only the *most recent* survey
  per track, rather than the full history it keeps today. Explicitly
  deferred to its own dedicated discussion rather than decided as a side
  effect of the CSV work.
- **Step 14's exact scope** — see above.
- **Database portability — how to move/share the database between
  computers, properly.** Raised directly by the developer after manually
  copying `ship-management.db` between machines for a second time (see
  `docs/machine-setup.md`) — the `%APPDATA%`-hunting, copy-by-hand process
  works, but isn't something to keep doing forever. Two directions were
  raised; worth weighing against each other when step 15 is actually
  designed, rather than picked ad hoc under pressure to move a file right
  now:
  - **An in-app Settings screen with Import / Export / Update-database
    controls** — a real backup/restore feature. This is exactly what step
    15 ("Audit trail viewer + backup/restore", `CLAUDE.md` §11) already has
    on the roadmap, so this isn't a new item so much as a reason to not
    keep deferring it. It also needs the `--data-dir`-style gap closed
    first (see Outstanding technical debt below) so the app can be pointed
    at a chosen file instead of always the one fixed `%APPDATA%` location.
  - **Keeping the live database file inside a synced cloud folder**
    (Google Drive, OneDrive, etc.) so every computer always sees the same
    copy automatically. Worth flagging clearly before anyone tries it:
    SQLite is not designed to be edited from inside an auto-syncing
    folder — if the app is writing to it on one machine while the sync
    client uploads or downloads it in the background, or if it's ever
    open on two machines at once, the usual result is a corrupted database
    or a "conflicted copy" file, not a clean merge. This also cuts against
    this project's own data rule #1 (`CLAUDE.md` §6) — UUID primary keys
    and the `origin_node`/`revision` columns exist specifically so that
    **two separate** offline databases can be merged safely later (the
    planned ship-to-shore sync), which is a fundamentally different,
    safer design than one file silently synced underneath the app by a
    third-party client. If cloud storage has a role here at all, it's as
    a place to drop an *exported backup file* (once the feature above
    exists), not as the live database's actual home.

## Outstanding technical debt worth knowing about

- **`CertificateListWidget.cpp` — 554 lines**, the single most
  over-guideline file in the project (guideline is ~300). The plausible
  next split (`fillRow()` and the surrounding column-filling code, the
  same shape as the `CertificateListActions`/`CertificateIoActions` splits
  already done) has been flagged across several rounds now and is
  increasingly worth doing as its own pass rather than folding into a
  future feature round.
- **No `--data-dir` startup override.** `main()` always points at the real
  `%APPDATA%` database — there's no way to run the app against a throwaway
  data directory for a manual visual check, which is exactly what
  `CLAUDE.md` §10.1 asks for and doesn't yet have a mechanism behind. The
  same gap exists at two more layers for attachments
  (`ManagedFileStore::setRootPathOverride()`, `VesselAttachmentRoots`).
  One `--data-dir` argument, plumbed through all three, would close all of
  it at once. Worth adding next time `main.cpp`/startup is touched for
  another reason.
- **The `%APPDATA%` vs. virtualized-path split is avoided, not understood.**
  Directly relevant to moving the app between machines — see the caveat in
  the machine-setup instructions. Worth properly investigating if a future
  backup/restore feature (step 15) needs to know the real path reliably,
  rather than continuing to just avoid automated commands near it.
- A handful of repository files sit over the ~300-line guideline with no
  clean seam yet found: `VesselRepository.cpp` (366), `CertificateRepository.cpp`
  (402), `EndorsementRepository.cpp` (414), `VesselAttachmentRoots.cpp`
  (330), `ManagedFileStore.cpp` (424). Watched each round, not urgent —
  each is one class's whole implementation, not an obvious multi-seam file.
- Minor environment quirks, low priority: no SQLite viewer installed (DB
  Browser for SQLite is the suggested one to grab); Qt Creator's F5
  debugging still doesn't stop on breakpoints (Ctrl+R run works fine, so
  this hasn't blocked anything yet).

## Immediate pending items

- **Confirm the list-number-prefix round actually shipped.** Commits
  `d9aae0b`/`8f16dfb` and the clean Release rebuild+redeploy were reported
  done but not yet confirmed pushed to `origin/main` — worth a quick
  `git log origin/main..HEAD` check.
- **Run the migration proof kit on the work laptop.** Your own chosen plan
  was to run `dist/migration-proof-kit/` against the work laptop's real
  database before migration 011 ships there — not yet reported back as
  done.
- **V TRE CSV — two rows still blocked.** No. 19 (Safety Construction
  Certificate) and No. 39 (Ballast Water Management Certificate) each have
  an annual-survey date that predates the certificate's own current issue
  date — needs either a corrected survey date or confirmation that the
  issue date itself is right. No. 19's unusually short ~2-month issue-to-
  expiry window is also worth double-checking against the source S-14
  sheet while you're there.
- **V ALDO CSV — clean.** All 214 rows validated; ready to import as-is.
- Once V TRE's two rows are fixed, re-run the same validator (or just the
  import dialog) to confirm both files go in cleanly before relying on
  them.
