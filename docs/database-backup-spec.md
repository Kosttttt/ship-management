# Step 15 — Database backup & restore

Version 1.0. The last feature of Certificate Control. Decided with the
developer:
- **Database only.** Certificate documents stay in each vessel's own
  attachment folder and are copied separately.
- **Manual backup and restore in Settings, plus an automatic daily backup**
  to a folder the developer chooses (e.g. a Google Drive folder).
- The audit trail viewer is deferred, not part of this step.

---

## 1. Why, and the one rule that keeps it safe

Today moving to another computer means finding `ship-management.db` by hand
(`docs/machine-setup.md`), and the only backup is whatever copy someone
remembered to make.

The rule this whole design follows: **the program never guesses where its
database is.** `CLAUDE.md` §10.1 exists because a "back up, delete,
restore" sequence run from outside the app used the plain `%APPDATA%` path,
while the running app was using a different, virtualized copy — and a real
database was lost. So:
- **Backing up** reads through the program's own open connection.
- **Restoring** swaps files at startup, in the folder
  `Database::defaultFilePath()` returns — the same call `main()` uses to
  open the database, so it is the right folder by construction.
- **Nothing is ever deleted** by restore: the database being replaced is
  kept beside it as a safety copy.

## 2. Making a backup — `VACUUM INTO`

SQLite's `VACUUM INTO 'file'` writes a complete, consistent copy of the
open database to a new file, safely, while the program keeps running. No
copying of a live file, no path guessing.

Two details that matter:
- **Write to a temporary name, then rename.** Write
  `<name>.db.tmp` in the target folder, then rename to `<name>.db`. A
  Google Drive / OneDrive client must never see and upload a half-written
  file. `VACUUM INTO` also refuses an existing target, so the temporary
  name avoids that too; if the final name already exists (the user chose
  it in a save dialog and confirmed overwrite), replace it only after the
  temporary file is complete.
- **Check the result.** Open the new file on a separate, read-only
  connection and run `PRAGMA integrity_check`. A backup that fails the
  check is deleted (it is our own temporary file) and reported as a
  failure — never left looking like a good backup.

Filenames, so a folder of backups reads sensibly and two computers can
share one Google Drive folder without clashing:
- manual: `ship-management-<node>-<yyyy-MM-dd_HHmm>.db`
- automatic: `ship-management-auto-<node>-<yyyy-MM-dd>.db`

`<node>` is the installation's node id (`OFFICE`, or `VESSEL-<IMO>`).

## 3. Settings screen — a "Database backup" section

A new section on the Settings page, built as its own widget the way
`AttachmentFolderSettings` already is (keeps `SettingsPage.cpp` small).

- **Back up now…** — save dialog, suggested name per §2, starting in the
  last folder used for backups. On success: "Backup saved: <path>
  (<n> vessels, <n> certificates)."
- **Restore from backup…** — §5.
- **Automatic backup** — a checkbox "Back up automatically once a day", a
  folder field with a Choose… button, and a line showing the last
  automatic backup ("Last automatic backup: 11 Nov 2026, 09:12" or
  "None yet").

**Where these settings live: `QSettings`, not the database.** The backup
folder is a path on *this* computer. If it lived in `app_setting`, a
backup restored on another computer would arrive pointing at a folder that
doesn't exist there. Same reasoning as `TableHeaderSettings` and the
attachment chooser's last folder.

Both installation modes get the section — a vessel's computer needs
backups as much as the office does.

## 4. Automatic daily backup

- Runs at startup, **after** migrations (so the backup has the current
  schema) and after the installation is resolved.
- Only if it is switched on, a folder is set, and there is **no automatic
  backup for this node dated today** already in that folder. Deciding
  "already done today" from the files in the folder, not a stored date,
  means a folder that was emptied or changed simply gets a fresh backup.
- **Keeps the 10 newest** automatic backups for **this node** and deletes
  older ones. It only ever deletes files matching its own automatic
  pattern for its own node — never manual backups, never another
  computer's backups, never anything else in the folder.
- **Never blocks or stops startup.** If the folder is missing (Google Drive
  not running, USB drive unplugged) or the backup fails: log it, show a
  short message in the status bar, carry on.

## 5. Restoring

### 5.1 Choosing and checking the file

**Restore from backup…** opens a file dialog. The chosen file is checked
on a separate, read-only connection — the live database is not touched
yet:
1. It is a SQLite database and `PRAGMA integrity_check` passes.
2. It has this program's tables (`schema_version`, `installation`,
   `vessel`, `certificate`) and exactly one `installation` row.
3. **Its schema is not newer than this program.** The highest version in
   its `schema_version` must not exceed the highest migration compiled
   into this program. A newer backup is refused: "This backup was made by
   a newer version of the program. Update this program first." An *older*
   backup is fine — the usual migrations bring it up to date on the next
   start.

Any failure → a plain message saying which check failed, and nothing else
happens.

### 5.2 Confirming

Show what is about to happen, side by side, before anything is written:

```
Restore this backup?

                 This computer now        The backup
Mode             OFFICE                   OFFICE
Vessels          3                        3
Certificates     611                      598
Last change      11 Nov 2026 14:02        09 Nov 2026 18:40

Everything on this computer will be replaced by the backup.
The current database is kept as a safety copy.
The program will restart to finish.
```

If the backup's mode or node differs from this computer's, say so
plainly above the table — e.g. "This backup is from the vessel V UNO; this
computer will become that vessel's installation." Default button: No.

### 5.3 Doing it — at the next start, not now

Repositories and screens all hold the open connection, so the file cannot
safely be swapped mid-session. Instead:
1. Copy the checked backup into the data folder as
   `ship-management.restore-pending.db` (temporary name, then rename).
2. Restart the program (`QProcess::startDetached` on the program's own
   path, then quit). If the restart can't be launched, say "Start the
   program again to finish restoring" and quit.
3. At startup, in `main()`, **before** the database is opened: if
   `ship-management.restore-pending.db` exists in the data folder,
   - rename the current `ship-management.db` (and any `-journal`, `-wal`,
     `-shm` beside it) to `ship-management.before-restore-<yyyy-MM-dd_HHmmss>.db`;
   - rename the pending file to `ship-management.db`;
   - if the second rename fails, put the first one back, report it, and
     open the original database as normal — never leave the folder with no
     database.
4. Then open and migrate as usual, and log what happened.

Safety copies are not deleted automatically.

## 6. Where the code goes

- **`core/`** — the backup and restore logic, no widgets: making a checked
  backup from a connection, inspecting a backup file (the §5.1 checks and
  the §5.2 summary), the startup swap, and the automatic-backup decision
  and pruning. **Every function that touches files takes the folder as a
  parameter**, so tests run on a `QTemporaryDir`; only `main()` passes the
  real data folder (from `Database::defaultFilePath()`).
- `MigrationRunner` needs to expose the highest migration it knows about,
  for §5.1 check 3.
- **`app/`** — the Settings section and the restore confirmation.
- `main()` — the startup swap (§5.3 step 3) and the automatic backup (§4).

Follow `CLAUDE.md` §9 as amended: helper classes over long files.

## 7. Tests

All on temporary folders and temporary databases — **never** the real data
folder or the real settings (`CLAUDE.md` §10.1; the settings-file override
pattern from `TableHeaderSettings`).

- Backup round trip: seed a database, back it up, open the backup — same
  rows. The temporary `.tmp` file is gone afterwards.
- A backup that fails `integrity_check` is not left behind.
- Inspect refuses: a non-database file; a database without our tables;
  one with no or two `installation` rows; one with a newer schema version.
  Accepts an older schema version.
- Startup swap: pending file present → it becomes `ship-management.db`, the
  old one becomes a `before-restore-…` file with the same contents. No
  pending file → nothing changes. Second rename failing → the original is
  back in place.
- Automatic backup: due when nothing for today; not due when today's file
  exists; folder missing → no crash, reported; pruning keeps the 10 newest
  of **this node only** and leaves manual backups, other nodes' backups
  and unrelated files alone.

The restart itself and the dialogs are checked by the developer by hand.

## 8. Not changed

Database schema (no migration in this step), certificate behaviour,
attachment folders, CSV import/export.

## 9. After this ships

`docs/machine-setup.md` gets rewritten around Back up → Restore instead of
finding the file by hand. Done alongside §10.

## 10. Close-out additions (after `d702988` was reviewed)

### 10.1 A side file that won't move cancels the restore

`RestoreSwap::applyPending()` today: if a `-journal`/`-wal`/`-shm` beside
the current database can't be renamed into the safety copy, it is
**deleted** and the restore carries on. Its comment assumes the database
was closed cleanly — but these side files only exist when it *wasn't*
(a crash, a power cut). A `-journal` left by a crash holds what is needed
to repair the database; deleting it can leave the safety copy damaged.

Change: if any existing side file can't be moved, **undo and stop** — move
back whatever side files were already moved, rename the safety copy back
to `ship-management.db`, leave the pending file where it is, and return
`Failed` with a plain message ("The restore has not happened. Your data is
untouched. A file next to the database is in use — close any program that
might be using it and start again."). A restore that doesn't happen is
always safe; a damaged safety copy is not.

Test (Windows-only like `aFailedSwapPutsTheOriginalBack`, same `QSKIP`
reason): a locked side file → `Failed`, the original database and its side
file are back under their original names with the same contents, the
pending file is still there.

### 10.2 Restore straight from the first-run setup

On a brand-new computer the program opens the first-run wizard, and
Settings — where Restore lives — can't be reached until setup is finished.
Moving to a new computer would mean inventing a fake installation just to
restore over it.

Add to the wizard's first page (installation mode): a button **"Restore
from a backup instead…"**. It runs exactly the Settings restore path —
choose file, the §5.1 checks, a confirmation (no "this computer now"
column, since there is nothing yet), `RestoreSwap::stage()`, restart. Reuse
the same classes; don't duplicate the checks. The empty database the
program created before the wizard opened becomes the `before-restore-…`
safety copy at the next start, which is harmless.

If restarting can't be launched, say "Start the program again to finish
restoring" and close the wizard as cancelled.

Test what can be tested without a dialog (the button exists on the mode
page; the restore path it calls is the already-tested one). The developer
checks the full flow by hand on a second computer or a fresh user account.
