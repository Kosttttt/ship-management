# Settings & `app_setting` Spec

Step 8 of the build order (`CLAUDE.md` §11). Makes the alert thresholds
`computeCertificateState()` has used as hardcoded `AlertThresholds()` defaults
since step 6 into something the user can actually edit, and lays the schema
groundwork step 9 (Alerts) needs for daily-toast tracking.

Read alongside `docs/certificate-control-spec.md` §4.3 (what the thresholds
mean) and `docs/certificate-list-status-spec.md` §8 (the current hardcoded
call site this step replaces).

## 1. Purpose

Three numbers currently exist only as source code (`AlertThresholds`'s default
member initializers: 30/60/90). This step gives them a real home in the
database, a screen to edit them, and wires that value into the one place that
currently reads the hardcoded default.

## 2. Scope

In scope:
- A new core-owned `app_setting` table — a singleton row, same pattern as
  `installation`.
- `core/AppSetting` (domain struct) and `core/AppSettingRepository` (read the
  one row, update it with validation).
- A new `Settings` screen, reachable from the sidebar in both installation
  modes.
- `CertificateListWidget::reload()` reads real thresholds from
  `AppSettingRepository` instead of default-constructing `AlertThresholds()`.

Out of scope, deferred:
- Anything that *reads* the daily-toast-last-shown column — that's step 9.
  This step only adds the column, seeded `NULL`.
- Any other module reading thresholds — there is only one consumer today.

## 3. Data model

`migrations/006_create_app_setting.sql`. Singleton enforcement must be
**identical to `migrations/001_create_installation.sql`'s mechanism** — copy
that pattern exactly rather than inventing a second way to enforce "exactly
one row," so the two singleton tables in this schema behave the same way.

Columns beyond the standard audit set (`CLAUDE.md` §6.5):

```
critical_days          INTEGER NOT NULL DEFAULT 30
expiring_soon_days     INTEGER NOT NULL DEFAULT 60
due_soon_days          INTEGER NOT NULL DEFAULT 90
last_alert_toast_date  TEXT        -- nullable ISO date; NULL = "never shown".
                                    -- Added now, read/written starting step 9.
```

Two `CHECK` constraints, as the final backstop behind the repository's own
validation (§5):
- `critical_days > 0 AND expiring_soon_days > 0 AND due_soon_days > 0`
- `critical_days < expiring_soon_days AND expiring_soon_days < due_soon_days`

Unlike `installation`, this table's single row is **seeded by the migration
itself**, not created later by a wizard — there's no user decision needed to
create it (the defaults are already the right starting values), so the
`INSERT` with the three DEFAULT values and a `NULL` toast date belongs in
`006_create_app_setting.sql` directly. The table is never empty from the
moment this migration runs.

## 4. Domain

`core/AppSetting.h` — a plain struct, **no dependency on the certificates
module**:

```cpp
struct AppSetting {
    QString id;
    int     criticalDays     = 30;
    int     expiringSoonDays = 60;
    int     dueSoonDays      = 90;
    QDate   lastAlertToastDate; // invalid QDate = never shown
    // ...standard audit fields, same shape as Vessel/Certificate...
};
```

No validation logic lives in the struct itself (same convention as
`Certificate`/`Vessel`) — that's the repository's job, per §5.

## 5. Repository

`core/AppSettingRepository`, following the same shape as
`InstallationRepository`:

- `read(AppSetting*) -> bool` — there is always exactly one row once the
  migration has run, so this is a plain `SELECT` with no scope filtering
  (there is nothing module- or vessel-specific about it). If the row is
  somehow missing (shouldn't happen, but don't assume), return `false` with
  a clear error rather than crash — the certificates module's read of this
  should fall back to `AlertThresholds()` defaults on failure, same
  "still shows something reasonable rather than breaking" pattern step 7
  used for a certificate whose endorsements couldn't be read.
- `update(const AppSetting&) -> bool` — validates before writing:
  - each value is a positive integer
  - `criticalDays < expiringSoonDays < dueSoonDays`, with a friendly message
    naming which pair is out of order, not just "invalid"
  - bumps `revision`, touches only `updated_at`/`updated_by`, exactly like
    every other `update()` in this project.
- No `create()` — the row is seeded by the migration, not created at
  runtime.

## 6. Screens

`app/SettingsPage` (core-owned UI, same folder level as `VesselListWidget`,
not under any module's `ui/`). A single form, no dialog wrapper — it edits
the one settings row directly, the same way `VesselDetailPage` edits the
one vessel in VESSEL mode:

- Three `QSpinBox` fields (Critical / Expiring Soon / Due Soon, in days),
  each with a sane range — minimum 1, maximum a generous but bounded upper
  limit (e.g. 3650, ten years) so a mistyped value can't be absurd.
- Live validation: the Save button disables itself the moment the three
  values stop satisfying `critical < expiringSoon < dueSoon`, with a short
  inline hint explaining why — the same "widget enforces the rule before the
  repository has to" pattern used for the no-expiry checkbox in step 5. The
  repository's own check stays as the backstop for anything that reaches it
  another way.
- Loaded from `AppSettingRepository::read()` on construction; Save calls
  `update()`.
- A new fixed entry in `MainWindow`'s sidebar, labelled "Settings", visible
  in both OFFICE and VESSEL mode — no role restriction, per the project's
  standing rule that role-based access is applied on top of finished
  functionality at the end, not designed around now.

## 7. Wiring into the certificate list

`CertificateListWidget` gains an `AppSettingRepository&` constructor
parameter (same threading pattern used for `EndorsementRepository` in step
6). In `reload()`, replace the current `const AlertThresholds thresholds;`
default construction with:

```cpp
AppSetting setting;
AlertThresholds thresholds; // falls back to the 30/60/90 defaults on failure
if (m_appSettings.read(&setting)) {
    thresholds.criticalDays     = setting.criticalDays;
    thresholds.expiringSoonDays = setting.expiringSoonDays;
    thresholds.dueSoonDays      = setting.dueSoonDays;
}
```

This is the one-line-per-field translation described in the architectural
note above — the only place an `AppSetting` (core) becomes an
`AlertThresholds` (certificates module), and it happens on the certificates
side of the boundary, not core's.

`CertificatesModule` and `MainWindow`/wherever widgets are constructed need
the same repository threaded through as every other repository already is.

## 8. Edge cases to test

1. Reading `app_setting` immediately after a fresh migration run returns the
   seeded defaults (30/60/90, `lastAlertToastDate` invalid).
2. `update()` refuses `criticalDays >= expiringSoonDays`, with a message
   naming the two fields, and leaves the stored row unchanged.
3. `update()` refuses `expiringSoonDays >= dueSoonDays`, same shape.
4. `update()` refuses a zero or negative value for any of the three fields.
5. A valid `update()` (e.g. 14/45/75) is stored, re-`read()`s back correctly,
   and bumps `revision`.
6. The raw SQLite `CHECK` constraint text never reaches the user — same test
   pattern as every other repository in this project.
7. `SettingsPage`'s Save button is disabled when the three spin boxes are
   edited into an invalid order, and re-enables once corrected.
8. `CertificateListWidget` actually uses an edited threshold: with
   `criticalDays` changed to e.g. 5, a certificate 10 days from expiry no
   longer shows Critical (previously would have, under the 30-day default).
9. If `AppSettingRepository::read()` fails, `CertificateListWidget` still
   renders using the hardcoded `AlertThresholds()` defaults rather than
   crashing or showing nothing.

## 9. What's explicitly not built this step

- Nothing reads or writes `last_alert_toast_date` yet — schema only.
- No sidebar badge, no toast itself — step 9.
- No per-vessel or per-module threshold overrides — one global setting,
  same as the thresholds have always conceptually been.
