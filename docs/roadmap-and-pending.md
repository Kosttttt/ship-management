# Roadmap and pending items

A single-page "where are we" view, distilled from `CLAUDE.md` §11 and
`docs/PROJECT-STATUS.md`'s running log. Those two remain the full record —
this is the short version to orient a new session quickly.

## Now: Document Management — a simple Confluence

Spec `docs/document-management-spec.md` (version 2.0). The developer saw
the first step 1 — a table of uploaded files, built in `47179f5` — and
decided the module should work like Confluence instead: spaces, a page
tree, pages read and edited in place, history. That library is removed.

Steps: 1 spaces + page tree + editor + draft/publish (✅ built, `7717f6a`;
waiting for the developer's on-screen check); 2 history + compare;
3 attachments + search; 4 links + templates; 5 printed output (PDF, change
bars, Record of Revisions, List of Effective Pages, Print changed pages,
`.odt`); 6 office → vessel packages with the master's receipt.

Two kinds of space: **Manual** spaces publish a whole-manual revision
("SMS Manual Rev 8"); **Collection** spaces (circulars, forms, each
vessel's own space) publish page by page.

Worth remembering why manuals are written *in* the program: Qt's PDF
module can't be built with MinGW, so the program can't print chosen pages
of an uploaded PDF — but it can print any page it lays out itself.

## Certificate Control — complete ✅

Tagged `certificate-control-v1.0`, patched as `certificate-control-v1.0.1`
(`dd51c8f`): the term is now counted to the nearest year, so a certificate
a few days short of whole years no longer loses its first anniversary
(found by the developer on the V TRE S.M.C. and DOC). Everything in `CLAUDE.md` §11's build
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

- **Check on screen after v1.0.1:** V TRE No. 14 (S.M.C.) intermediate
  window 30 Sep 2027 – 30 Mar 2029; No. 13 (DOC) annual window open since
  21 Sep 2026 — record this year's annual verification if it's done.
- **Documents step 1 on-screen check** (see the round's report).
- **Delete the old `Documents` folder** from the removed version 1.0 by hand —
  the program no longer knows those files exist.

- **Work laptop:** set it up from a backup with the new
  `docs/machine-setup.md` (setup-time restore). Also the last on-screen check
  of step 15 that couldn't be done on this computer.
- **V TRE CSV, rows No. 19 and No. 39:** the annual-survey date is before
  the certificate's issue date, which the app refuses. Needs the correct
  survey date or the correct issue date — only the latest annual date is
  needed now. You can also simply fix them in the app directly.
- **V ALDO CSV:** clean, 214 rows, ready to import if not done already.
