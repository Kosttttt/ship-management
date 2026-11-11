# Running the Certificate module on another computer

This is the simple version: putting the already-built program on a second
computer so you can work with certificates there, carrying over the data
that's already entered (V UNO / V TRE / V ALDO). Nothing in this doc needs
Qt, a compiler, CMake, or git on the new machine — that's only needed for
*developing* the app, not running it, and isn't what you're after here.

## Step 0 — make sure what you're about to copy is current

The portable, run-anywhere copy of the app lives at
`D:\dev\ship-management\dist\ShipManagement\` on your dev machine — it's a
plain folder (the `.exe` plus the Qt DLLs `windeployqt` bundled next to it),
not an installer, so copying the whole folder *is* installing it.

`dist\` is a plain copy, not something the build updates by itself —
rebuilding the code does **not** refresh it. So it is only as current as
the last time someone redeployed it, and the latest round in
`docs/PROJECT-STATUS.md` says whether that round redeployed. If it's
behind, the new computer ends up running an **older** build than the one
your data already assumes — for example, one that doesn't know about
archived vessels, or doesn't sort a List Number like `S1` correctly.

A quick check you can do yourself: compare the "Date modified" of
`dist\ShipManagement\ShipManagement.exe` with
`build-release\ShipManagement.exe`. If `build-release`'s is newer, `dist\`
is stale. To refresh it, on the dev machine:
```powershell
cmake --build build-release
Copy-Item build-release\ShipManagement.exe dist\ShipManagement\ShipManagement.exe -Force
windeployqt --release --compiler-runtime dist\ShipManagement\ShipManagement.exe
```
(Copy the exe over **before** running `windeployqt` — doing it the other
way round bundles fresh DLLs around a stale exe with no warning, which has
bitten this project once already.) Launch it once on the dev machine
afterwards and confirm it opens, before trusting it enough to copy
elsewhere.

## Step 1 — copy the program itself

Copy the whole `dist\ShipManagement\` folder to the new computer — USB
drive, cloud-synced folder, network share, whatever you'd normally use.
It needs nothing pre-installed on the target machine; the folder is
self-contained. Don't run it yet.

## Step 2 — find the real data on the old machine

Two things currently exist only on the old machine:

1. **The database file**, `ship-management.db`. `CLAUDE.md`'s Environment
   table says this lives at
   `%APPDATA%\Ship Management\Ship Management System\`. **Important
   caveat**, already hit once and written up in `docs/PROJECT-STATUS.md`'s
   Environment section: on at least one of your machines, the plain
   `%APPDATA%` path and the folder the *running app* actually reads from
   turned out to be two different files with the same-looking name
   (Windows path virtualization). Before copying anything, confirm which
   folder the app is really using — the rotating log file at
   `...\logs\ship-management.log` is the reliable way to check. Don't
   assume the plain `%APPDATA%` path is the real one just because it looks
   right.
2. **Each vessel's attachment folder.** Since the attachment addendum,
   this is configured *per vessel*, not fixed to wherever the database
   sits — check each vessel's settings inside the app to see where its
   documents actually live on the old machine. There can be more than one
   folder, one per vessel, and they don't have to be anywhere near each
   other.

Nothing else needs to move. The log file isn't meaningful to carry over,
and the schema version is already embedded inside the database file itself
— the app will apply any migration it hasn't seen yet automatically, the
first time it opens the carried-over database (this already happened
safely on both your machines when migration 011 shipped, so it's a proven
path, as long as Step 0's exe is the one that ships with it).

## Step 3 — copy the data across, carefully

- **Close the app completely on the old machine first.** Don't copy the
  database file out while the app is still running — SQLite sometimes
  leaves extra `-wal` and `-shm` files sitting next to the main `.db` file
  when a connection is open; if either is there alongside
  `ship-management.db`, copy them too, in the same copy, so nothing is
  left half-written.
- **Copy by hand** — drag-and-drop, USB drive, cloud folder, whatever you'd
  normally use. Do **not** have Claude (this session or Claude Code) run an
  automated copy/overwrite command against either machine's real data
  folder for this — that's exactly the destructive-command rule in
  `CLAUDE.md` §10.1, written after a real database was lost that way once
  already. A manual copy you do yourself and can watch happen is the safe
  version of this step.
- **On the new machine**, run the program once from the copied
  `dist\ShipManagement\` folder — this creates its own empty database and
  first-run wizard, same as any brand-new install. Close it again, then
  replace that empty `ship-management.db` with the real one you just
  copied over (and its `-wal`/`-shm` files, if any). Copy each vessel's
  attachment folder across the same way, to wherever you want it to live
  on the new machine.
- **Re-point each vessel's attachment folder** in the app's Settings on
  the new machine to wherever you actually put it. This path is
  deliberately stored as an absolute, per-machine setting
  (`CLAUDE.md` §6.6 rule 9) and is never carried across automatically, by
  design — the same database file can sit on a machine where the
  attachments live somewhere completely different.

## Step 4 — verify, don't assume

- Launch the app on the new computer and check it yourself: open the
  vessel list, spot-check a certificate's survey history, and confirm an
  attachment actually opens — for more than one vessel if you have more
  than one. A clean launch with no error isn't proof the copy is right;
  looking at the data is.
- **Don't delete anything on the old machine** until the new computer is
  verified working end to end. There's no sync or backup/restore feature
  yet (`CLAUDE.md` §11, step 15 and ship-to-shore sync are both still
  unbuilt), so until the new machine is confirmed good, the old machine's
  files are the only copy that exists.
