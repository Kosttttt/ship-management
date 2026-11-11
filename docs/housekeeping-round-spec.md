# Housekeeping round — file sizes and small leftovers

Version 1.0. Round 2 of closing out Certificate Control
(`docs/roadmap-and-pending.md`). Mostly no behaviour change: every existing
test passes as it is, except tests that are only *moved* (§4). The one
visible change is §5, the attachment chooser.

---

## 1. The rule this round applies

`CLAUDE.md` §9, as amended by the developer: when a class outgrows ~300
lines, move a self-contained piece into a small helper class **in the same
layer**, rather than spreading one class across two files. A helper that
holds SQL stays in the repository layer and is used only by its own
repository.

For every file below: report the line count before and after. If a file
is still over ~300 after the seams named here, **report the number and
stop** — don't invent a seam to hit the target. A helper with no real
boundary is worse than a long file.

## 2. `core/VesselRepository.cpp` (557 lines)

1. **IMO-number conflict check** → a new helper in `core/`, e.g.
   `VesselImoNumberCheck`: `findImoNumberHolder()` plus
   `imoNumberConflictMessage()` and the `ImoNumberHolder` type (~75 lines,
   no dependency on the rest of the class). `VesselRepository` keeps the
   same public behaviour and messages; `create()`, `update()` and
   `restore()` call the helper.
2. **Only if still well over ~300 after that:** the archive pair —
   `checkArchiveChangeAllowed()` and `writeArchivedFlag()` — into a second
   helper, used only by `VesselRepository::archive()`/`restore()`. Public
   `archive()`/`restore()` stay on `VesselRepository`.

## 3. `modules/certificates/ui/CertificateListWidget.cpp` (554 lines)

1. **Row rendering** → a helper in the same `ui/` folder, e.g.
   `CertificateRowRenderer`: `fillRow()` and the item/colour helpers in the
   anonymous namespace at the top of the file that only `fillRow()` uses.
   Same shape as the `CertificateListActions`/`CertificateIoActions`/
   `CertificateDeleteActions` splits already in this folder. The widget
   keeps `reload()`, which calls the renderer per row.
2. **Only if still well over ~300 after that:** the next seam is yours to
   name, from the code — the constructor's toolbar/button construction is
   the likely candidate. Report what you chose and why.

Cell contents, colours, sort keys and column order must be byte-for-byte
the same as before. `tst_CertificateListWidget`, `tst_CertificateListStatus`
and `tst_CertificateListColumns` passing unchanged is the proof.

## 4. Test files over ~300

- `tst_CertificateStateSurvey.cpp` (426): the matching cases (latest
  survey covers earlier ones, late note, the accepted trade-off, the
  before-first-window case, the real Load Line case) → a new suite
  `tst_CertificateStateSurveyMatching.cpp`, registered in
  `tests/CMakeLists.txt`.
- `tst_CertificateCsvImportSurveys.cpp` (430): split only if a clean seam
  exists; otherwise report and leave it.

**Proof nothing was lost in a move:** the total number of test functions
across the old and new file must equal the number before the split. State
both numbers in the report.

## 5. Attachment chooser remembers the last folder

Today `AttachmentChooser::chooseFile()` and `chooseFolder()` always open
at Documents — the file's own comment: *"Documents rather than the
last-used folder, which nothing stores yet."*

New behaviour:
- After a **successful** pick (not on Cancel), remember a folder:
  for a file, the folder it is in; for a folder of documents, that
  folder's **parent**, so next time the chosen folder is visible among its
  neighbours. One remembered folder, shared by both.
- Next time either dialog opens, start there.
- **Fall back to Documents** when the remembered folder no longer exists,
  **or** when it is the current vessel's `attachment_root_path` or inside
  it. This keeps the safety reason in the existing comment: the managed
  folder is the one place a source must never be picked from (copying a
  file onto itself).

Where it's stored: `QSettings`, like `TableHeaderSettings` — a per-machine
UI preference, never the database. To check against the vessel's folder,
the chooser needs that path from its callers
(`CertificateAttachmentActions`, `AttachmentSourceField`); how it's passed
is your call.

Tests: use a throwaway settings file, the same override pattern
`TableHeaderSettings::setSettingsFileOverride()` uses, so no test writes to
the developer's real settings (`CLAUDE.md` §10.1). Cover: nothing
remembered → Documents; remembered and still there → it; remembered but
deleted → Documents; remembered inside the vessel's attachment folder →
Documents; Cancel remembers nothing. The dialog itself needn't be driven —
test the "where to start" decision as a function.

## 6. Two small code fixes

- **`core/TransactionScope.h`:** `commit()` clears its "open" flag
  *before* calling `QSqlDatabase::commit()`, so a failed commit leaves the
  destructor unable to roll back. Clear it only after the commit succeeds.
  (Not seen in practice; the one caller already reports a failed commit.)
- **`app/MainWindow.cpp`:** uses `QSignalBlocker` (line ~298) without
  `#include <QSignalBlocker>`; it works only through another header. Add
  the include.

## 7. Dropped from this round, with the reason

**First-run wizard, duplicate IMO number.** Listed as debt since step 4,
but it can't happen: the wizard only runs when there is no installation
row, and vessel rows only exist after an installation has been set up —
so the vessel table is always empty when the wizard writes to it. Nothing
to fix; recorded here so it isn't raised again.

## 8. Not changed

Database schema, any behaviour other than §5, CSV format, the survey rule.
