# Manual test checklist — steps 1 through 10

This is a walkthrough for clicking through everything built so far, as a
real user would. It's a confidence/UX check, not a substitute for the
automated tests — those already run every edge case in each spec's "§N edge
cases" section through `ctest`, with 100% passing as of step 10 (24 test
suites). This checklist is about *using* the app, not re-proving the logic.

## Before you start: one safety rule

**Never run a manual delete, backup, or overwrite command against the
app's real data folder** (`%APPDATA%\Ship Management\...` or wherever it
actually resolves to — step 7's incident showed those two can silently be
different files on this machine). That rule is about commands run *outside*
the app, in PowerShell or a file explorer.

Using the app itself — clicking Add, Edit, Save, Renew — is completely
safe and exactly what it's built for. Go ahead and enter real-looking test
data through the UI; that's the normal, intended way to use it.

One thing to know going in: there is currently **no delete** for vessels or
certificates in the UI (soft-delete exists in the database, but no screen
exposes it yet). So any test vessel or certificate you add now will stick
around. Two reasonable ways to handle that:
- Name test entries obviously, e.g. `TEST — Alpha`, so they're easy to spot
  and ignore later.
- When you're ready to start using the app for real, come back here first
  rather than deleting the database file yourself — given the step 7
  incident, finding the *real* data file this app uses on this machine
  needs a careful look, not a guess.

## 1. Opening the app

In Qt Creator: open the project, make sure the Qt 6.11.1 MinGW 64-bit kit
is selected (bottom-left kit selector), then **Ctrl+R** to build and run.
(**F5** also runs it but with debugging attached — remember breakpoints
don't actually stop on this machine yet, so Ctrl+R is the normal way.)

What to check: the window opens with no error dialog, and the sidebar shows
**Vessels** (or your one vessel's screen, if this installation is in
VESSEL mode), your registered modules, and **Settings** at the bottom.

## 2. Core infrastructure (steps 1-2)

Nothing to click here — this is the plumbing everything else sits on. If
the app opened without an error dialog, it's working. If you want to peek:
the log file is at `logs\ship-management.log` inside the app's real data
folder, and it should show startup lines with UTC timestamps.

## 3. First-run wizard (step 3)

This already ran once on this machine, when the app's `installation` table
was first created — you won't see it again on this installation, and
that's correct, not a bug (it's meant to run exactly once). Skip this one;
there's nothing to click.

If you ever do want to see the wizard again — say, to double-check it still
works before handing this app to someone else's machine — that needs a
fresh, empty data folder, which is a bigger step than this checklist
covers. Ask me first when that's actually wanted; it is not something to
improvise with a delete command.

## 4. Vessels (step 4) — OFFICE mode only

(If this installation is in VESSEL mode, there's exactly one vessel and no
"Vessels" list — skip to the certificates section below, which is what
VESSEL mode shows directly.)

1. Click **Vessels** in the sidebar.
2. Click **Add Vessel**. Try saving with the IMO number field empty or
   wrong, then type a few digits — the field should visibly reject an
   invalid check digit as you type.
3. Fill in a name (e.g. `TEST — Alpha`) and a valid-looking IMO number,
   save.
4. Open the toolbar vessel selector at the top — your new vessel should
   already be there, no restart needed.
5. Double-click the vessel in the list to edit it, change the name, save,
   and confirm the toolbar selector picks up the rename too.

## 5. Certificates (step 5)

1. With your test vessel selected, click **Certificates** (or whatever
   your module is named) in the sidebar.
2. Click **Add Certificate**. Fill in a name, pick a category, set an issue
   date and an expiry date, and try both survey checkboxes.
3. In the **No.** field, try typing a letter first (e.g. `A1`) — it should
   refuse the keystroke. Then type `15D` — that should go through cleanly.
4. Save. Confirm the row appears with columns **No. · Name · Status ·
   Expiry Date · Survey From · Survey To · Days Left**, and that clicking
   the **No.** header sorts it correctly even against something like `3A`
   vs `15D` (numeric, not alphabetical).
5. Add a second certificate with **no expiry date** (tick "does not
   expire") — e.g. a Tonnage Certificate. Confirm its Expiry Date cell
   reads "Does not expire" and it shows a sensible status (Valid or Not
   Required) rather than anything alarming.

## 6. Endorsements (step 6)

1. Double-click your first certificate (the one requiring a survey) to
   open **Edit Certificate**.
2. You should see an **Endorsements** section with an **Add Endorsement**
   button.
3. Add one — pick a survey type (only the types this certificate actually
   requires should be offered), a date, and a place. Save it, then save
   the certificate.
4. Reopen the certificate and confirm the endorsement is still listed.

## 7. Status colors, filter, days left (step 7)

To see every color, add a few more test certificates (or edit existing
ones' expiry dates) to land in each bracket relative to today, using your
current thresholds (default 30/60/90 days unless you've changed them in
Settings):
- **Expired**: expiry date in the past.
- **Critical**: a few days from expiry (red).
- **Expiring Soon**: within ~60 days (orange).
- **Due Soon**: within ~90 days (light green).
- **Valid**: far out, no survey outstanding (no color at all).
- **Survey Overdue** / **Survey Due**: a certificate requiring annual
  surveys with no endorsement recorded for the current window (red /
  yellow).

Check:
- Each row's background color matches what you'd expect for its status.
- Tick **"Show only certificates needing attention"** — every Valid row
  should disappear; untick it and they come back.
- Click the **Days Left** header — it should sort numerically (a `-7`
  should sort before a `10`, not after it, the way plain text would get
  wrong).

## 8. Settings (step 8)

1. Click **Settings** in the sidebar.
2. Try setting the thresholds out of order (e.g. make "Expiring Soon"
   smaller than "Critical") — the **Save** button should grey out and a
   hint should name exactly which pair is wrong.
3. Fix the values, save.
4. Go back to the certificate list and switch vessels (or toggle the
   attention filter) to trigger a reload — confirm the colors now reflect
   your new thresholds. (The list doesn't repaint live the instant you
   save Settings — that's expected; it picks up the change on the next
   reload, which the Settings screen's own confirmation text already
   says.)

## 9. Alerts — banner and badge (step 9)

1. Make sure at least one certificate needs attention (any non-Valid
   status), then fully close and relaunch the app.
2. You should see a dismissible banner naming the vessel(s) with
   something outstanding, each with a **View** button.
3. Click **View** — it should switch to that vessel, turn on the
   "needs attention" filter, and switch the sidebar to the right screen,
   all in one click.
4. Check the sidebar entry for your module — it should show a live count
   next to its name.
5. Dismiss the banner, then close and relaunch the app **again on the same
   day** — the banner should **not** reappear (it's a once-a-day notice),
   but the sidebar count should still be accurate.

## 10. Renewal (step 10)

1. Select the certificate you added an endorsement to earlier (so it has
   survey history to lose).
2. The **Renew** button next to Add Certificate should be enabled. Select
   your no-expiry certificate instead — Renew should go grey/disabled, since
   there's nothing to renew.
3. Reselect the certificate with the endorsement and click **Renew**.
   The confirmation should name the certificate and mention how many
   recorded surveys will be cleared.
4. Click **No** (or Cancel) — confirm nothing changed: same dates, same
   endorsement still there.
5. Click **Renew** again, confirm **Yes** this time. The edit dialog that
   opens should say "Renew — <name>" in the title and should **not** show
   an Endorsements section at all.
6. Set a new expiry date (e.g. 5 years out) and save.
7. Confirm: the certificate's status is now Valid (or close to it), and if
   you reopen it with an ordinary double-click, the Endorsements section is
   back and empty.
8. Double-click a *different*, untouched certificate to confirm ordinary
   editing still opens the normal "Edit Certificate" dialog with its
   Endorsements section intact and nothing cleared.

## If something doesn't look right

Note exactly what you did, what you expected, and what actually happened
(a screenshot helps), and paste it back here rather than trying to fix it
yourself in the code — that's still Claude Code's job, with me reviewing
the result the same way I've reviewed every step so far.
