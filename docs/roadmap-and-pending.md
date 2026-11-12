# Roadmap and pending items

A single-page "where are we" view, distilled from `CLAUDE.md` §11 and
`docs/PROJECT-STATUS.md`'s running log. Those two remain the full record —
this is the short version to orient a new session quickly.

## Now: Document Management — design discussion

The second module. Design decided with the developer; spec
`docs/document-management-spec.md`. Steps: 1 library (versions, archive),
2 manual editor with change tracking, 3 manual output (PDF with change
bars, Record of Revisions, List of Effective Pages, Print changed pages,
`.odt`), 4 office → vessel packages with the master's receipt.

Worth remembering why manuals are written *in* the program: Qt's PDF
module can't be built with MinGW, so the program can't print chosen pages
of an uploaded PDF — but it can print any page of a manual it lays out
itself. Uploaded files open in the normal viewer.

## Certificate Control — complete ✅

Tagged `certificate-control-v1.0`. Everything in `CLAUDE.md` §11's build
order is done, except step 14 (Reports), which the developer dropped:
the one-vessel PDF from step 13 is enough.

The close-out, in order: "latest annual survey counts" (`6861d24`),
housekeeping (`05016eb`), database backup & restore (`d702988`, `433d1b4`),
category tabs (`0fff375`, red dot `60a5c41`). All checked on screen by the
developer.

## Carried over — not part of any module yet

Decided, not forgotten:
- **Audit trail viewer** — deferred out of step 15.
- **Ship-to-shore sync** and **role-based access control** — cross-module,
  deliberately later (`CLAUDE.md` §11).
- **`--data-dir` startup argument** — would let a manual check run against
  a throwaway database. Not needed so far.
- **Reports beyond the one-vessel PDF** (fleet overview, print preview,
  certificate history) — unscheduled; added to `CertificateReportBuilder`
  if a real need appears.
- **Badge and list share one calculation.** `CertificateAlertProvider` and
  `CertificateListRows` gather the same data separately; making the badge
  use `CertificateListRows` is the obvious next move, left out because it
  could change what the badge counts.

Code size, to tidy when that code is next touched (`CLAUDE.md` §9):
- `CertificateListWidget.cpp` (416) — grown three rounds running; the
  constructor is the bulk.
- `VesselRepository.cpp` (430), `ManagedFileStore.cpp` (424),
  `EndorsementRepository.cpp` (414), `CertificateRepository.cpp` (402),
  `VesselAttachmentRoots.cpp` (342), `MainWindow.cpp` (340).
- Tests: `tst_CertificateCsvImportSurveys.cpp` (430),
  `tst_CertificateListRows.cpp` (367).

Environment, low priority: no SQLite viewer installed (DB Browser for
SQLite suggested); Qt Creator's F5 doesn't stop on breakpoints (Ctrl+R
works).

## Your pending items

- **Work laptop:** set it up from a backup with the new
  `docs/machine-setup.md` (setup-time restore). Also the last on-screen check
  of step 15 that couldn't be done on this computer.
- **V TRE CSV, rows No. 19 and No. 39:** the annual-survey date is before
  the certificate's issue date, which the app refuses. Needs the correct
  survey date or the correct issue date — only the latest annual date is
  needed now. You can also simply fix them in the app directly.
- **V ALDO CSV:** clean, 214 rows, ready to import if not done already.
