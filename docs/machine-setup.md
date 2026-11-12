# Running the program on another computer

How to put the program on a second computer and bring your existing data
with it (V UNO, V TRE, V ALDO, …). The new computer needs nothing
installed — no Qt, no compiler, no git. Those are only for *developing*
the program.

There are three things to move:
1. **The program** — the `dist\ShipManagement\` folder.
2. **Your data** — one backup file, made by the program itself.
3. **Certificate documents** (scans, PDFs) — each vessel's attachment
   folder. These are *not* inside the backup.

## Step 1 — on the old computer: make a backup

Open the program → **Settings** → **Database backup** → **Back up now…**
Save the file somewhere you can carry it: a USB stick, or a Google Drive /
OneDrive folder. The name looks like
`ship-management-OFFICE-2026-11-12_0930.db`.

You don't need to find the database file inside `%APPDATA%` any more —
the program makes the copy from the inside, which is the safe way. (Hunting
for that file by hand is how a real database was lost once, in step 7.)

## Step 2 — copy the program

Copy the whole `D:\dev\ship-management\dist\ShipManagement\` folder to the
new computer (USB, cloud folder, network share). Copying the folder *is*
installing it — there's no installer.

**Make sure it's the latest build first.** `dist\` is a plain copy; rebuilding
the code does not refresh it. Quick check on the dev computer: the "Date
modified" of `dist\ShipManagement\ShipManagement.exe` should not be older
than `build-release\ShipManagement.exe`. If it is, refresh it:

```powershell
cmake --build build-release
Copy-Item build-release\ShipManagement.exe dist\ShipManagement\ShipManagement.exe -Force
windeployqt --release --compiler-runtime dist\ShipManagement\ShipManagement.exe
```

Copy the exe **before** running `windeployqt`, never after.

## Step 3 — on the new computer: restore

Double-click `ShipManagement.exe`. A brand-new computer opens the first-run
setup. On its first page, press **Restore from a backup instead…**, choose
the backup file from Step 1, check the summary (vessels, certificates,
last change), and confirm. The program restarts with all your data.

(If the computer already has the program set up, use **Settings → Database
backup → Restore from backup…** instead. The data it had is kept beside the
restored database as a `before-restore-…` safety copy — nothing is deleted.)

## Step 4 — the certificate documents

Each vessel has its own attachment folder, chosen in Settings, holding the
scanned certificates. Copy each of those folders to the new computer by
hand, then in **Settings** point each vessel at its folder's new location.
That path belongs to each computer and is never carried in a backup on
purpose — the folder may live somewhere completely different on the new
machine.

## Step 5 — check, then keep backing up

- Open a few vessels and certificates; open an attachment or two.
- In **Settings → Database backup**, tick **Back up automatically once a
  day** and choose a folder — a Google Drive folder works well. The program
  keeps the last 10 daily backups there and never touches anything else in
  that folder.
- Keep the old computer's data until the new one is checked.

## Never

- Never keep the **live** database itself inside a Google Drive / OneDrive
  folder, or open it on two computers at once — that is how SQLite files
  get corrupted. Backups in a synced folder are fine; the live database
  is not.
