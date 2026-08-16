# Alerts — implementation spec (step 9)

## §1 Purpose

Nothing currently tells you a certificate needs attention unless you open
the Certificates screen and look. This step closes that gap with two
things working together: a small persistent count on the sidebar, and a
once-a-day banner that names which vessels have something outstanding and
lets you jump straight to each one, already filtered.

Everything this step needs already exists: `computeCertificateState()`
(step 6) decides severity, the certificate list's "needs attention" filter
(step 7) already knows how to show only what's outstanding, the editable
thresholds (step 8) feed both, and `app_setting.last_alert_toast_date`
(step 8, unused until now) is exactly the column the once-a-day rule needs.

## §2 Scope

**In scope:**
- A generic `AlertProvider` interface (forward-declared since step 5,
  implemented for the first time here) that a module uses to report, per
  vessel, how many of its own things need attention.
- `CertificateAlertProvider`, the certificates module's implementation —
  walks every vessel in scope (the whole fleet in OFFICE mode, the one
  vessel in VESSEL mode — `VesselRepository::list()` already returns
  exactly the right set for either mode) and counts certificates whose
  `DisplayStatus != Valid`, the same rule the list's own filter uses.
- A count appended to the module's sidebar entry, e.g. "Certificates (3)",
  refreshed after anything that could plausibly have changed it.
- A dismissible in-app banner, shown at most once per calendar day, listing
  one row per vessel with something outstanding and a View button on each
  row that navigates straight to that vessel's filtered certificate list.
- A new `AppSettingRepository` method to record that today's banner was
  shown, without disturbing the threshold-only write `update()` already
  does (step 8 deliberately keeps those separate).

**Out of scope (see §10):**
- A native OS toast/notification — an in-app banner only, per this design
  discussion.
- A dedicated fleet-wide "everything needing attention" report screen —
  belongs with Reports (step 14).
- The banner or badge re-evaluating live while the app stays open across a
  calendar day boundary, or the banner live-updating its rows after it's
  shown — both are startup-time snapshots.
- A second module actually implementing `AlertProvider` — the interface is
  written to support one, but only `CertificateAlertProvider` exists today.

## §3 The `AlertProvider` interface

```cpp
// app/AlertProvider.h
#pragma once

#include <QString>

// One vessel's count from one module's AlertProvider (alerts-spec.md §3).
struct VesselAttentionCount {
    QString vesselId;
    QString vesselName;
    int     count = 0;
};

// What a module reports for the sidebar badge and the daily banner. A
// module owns what "needing attention" means for its own domain —
// MainWindow only ever asks "how many, for which vessels."
class AlertProvider
{
public:
    virtual ~AlertProvider() = default;

    // Only vessels with count > 0 belong in the result. Fleet-wide in
    // OFFICE mode, this installation's one vessel in VESSEL mode — never
    // scoped to whatever a toolbar selector happens to show right now.
    virtual QList<VesselAttentionCount> attentionByVessel() const = 0;
};
```

`app/IModule.h` already forward-declares `AlertProvider` and declares
`virtual QList<AlertProvider*> alertProviders() = 0;` — unchanged. Pointers
returned are non-owning: whichever `IModule` reports them owns the actual
`AlertProvider` instance for its own lifetime, the same way
`CertificatesModule` already owns `m_certificates`/`m_endorsements`.

## §4 `CertificateAlertProvider`

```cpp
// modules/certificates/CertificateAlertProvider.h
#pragma once

#include "app/AlertProvider.h"

class AppSettingRepository;
class CertificateRepository;
class EndorsementRepository;
class InstallationContext;
class VesselRepository;

class CertificateAlertProvider : public AlertProvider
{
public:
    CertificateAlertProvider(VesselRepository&          vessels,
                             CertificateRepository&     certificates,
                             EndorsementRepository&     endorsements,
                             AppSettingRepository&      appSettings,
                             const InstallationContext& installation);

    QList<VesselAttentionCount> attentionByVessel() const override;

private:
    VesselRepository&          m_vessels;
    CertificateRepository&     m_certificates;
    EndorsementRepository&     m_endorsements;
    AppSettingRepository&      m_appSettings;
    const InstallationContext& m_installation;
};
```

`attentionByVessel()`:

1. `VesselRepository::list()` — already scope-correct for either
   installation mode; nothing extra needed to respect VESSEL mode's "only
   ever see my own vessel" rule.
2. Read thresholds via `AppSettingRepository::read()`, same fallback the
   certificate list already uses: a failed read leaves the hardcoded
   30/60/90 defaults in place rather than skipping the computation.
3. `const QDate today = QDate::currentDate();` — read once, same principle
   as `CertificateListWidget::reload()`.
4. For each vessel: list its certificates, and for each certificate list
   its endorsements (tolerating a failed endorsement read the same way
   `reload()` does — treated as "no endorsements," not a hard failure);
   call `computeCertificateState()`; count anything whose `display !=
   DisplayStatus::Valid`.
5. Append `{vessel.id, vessel.name, count}` only when `count > 0`. Order
   follows whatever `VesselRepository::list()` returns (already
   alphabetical, matching the toolbar selector) — no extra sort needed.

This is the same N+1-per-vessel pattern step 7 already accepted for a
single vessel's list, now walking every vessel in scope. Deliberately not
batched into fewer queries at this fleet size, same call as before — flag
if it ever becomes measurably slow.

`CertificatesModule` gains two new members, constructed the same way
`m_appSettings` already is (owns what it needs, built from the database
connection and installation context it's handed):

```cpp
VesselRepository          m_vessels;       // new
CertificateAlertProvider  m_alertProvider; // new, built from the above
```

`alertProviders()` (currently returns `{}`) becomes `return
{&m_alertProvider};`.

## §5 Daily toast tracking

`AppSettingRepository::update()` deliberately never writes
`last_alert_toast_date` (step 8). A new, narrow method handles just that
column:

```cpp
// core/AppSettingRepository.h — new method
bool recordAlertToastShown(const QDate& shownOn);
```

```cpp
// core/AppSettingRepository.cpp
bool AppSettingRepository::recordAlertToastShown(const QDate& shownOn)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE app_setting SET"
        "  last_alert_toast_date = ?, updated_at = ?, updated_by = ?,"
        "  revision = revision + 1"
        " WHERE is_deleted = 0"));
    query.addBindValue(shownOn.toString(Qt::ISODate));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not record that the alert banner was shown:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}
```

Called by `MainWindow` immediately after the banner is actually shown, not
before — if nothing needed attention today, the date is left untouched, so
the banner is still eligible to show the next time something does. A
failure here is logged (`qWarning()`) but never surfaced to the user or
allowed to block startup: worst case, the banner shows again despite
already having appeared today, a harmless nuisance rather than a real
problem.

## §6 The sidebar badge

Each module's sidebar entry gains a count suffix when it has anything
outstanding: `"Certificates (3)"`. Zero stays plain `"Certificates"` — no
`"(0)"` hanging off a healthy fleet.

`MainWindow` needs to remember which sidebar row belongs to which module to
update this later:

```cpp
// MainWindow.h — new member
QHash<IModule*, int> m_moduleSidebarRows;
```

Populated inside the existing per-module loop in `buildSidebar()`, right
after `m_sidebar->addItem(...)`. A new private method,
`refreshAlertBadges()`, iterates `m_moduleSidebarRows`, sums
`attentionByVessel()`'s counts across every `AlertProvider` each module
reports, and rewrites that row's text (`module->displayName()`, plus `"
(%1)"` if the total is greater than zero).

Called once at startup (after the sidebar and all module screens exist),
and again whenever `CertificateListWidget` emits a new signal:

```cpp
// CertificateListWidget.h — new signal
signals:
    void certificatesChanged();
```

Emitted at the end of a successful `reload()` — the same trigger points
that already exist (vessel switch, filter toggle, add/edit certificate or
endorsement closing its dialog). `MainWindow` connects this to
`refreshAlertBadges()`. The badge is therefore live-ish within a session;
the banner (§7) is not — see §9 item 9.

## §7 The banner widget

```cpp
// app/AlertBanner.h
#pragma once

#include <QWidget>

class IModule;
class QVBoxLayout;

// alerts-spec.md §7. One row per (vessel, module) pair with something
// outstanding. A startup snapshot: populate() is called once, and the
// banner does not re-poll after that — see the module's own AlertProvider
// for what "outstanding" means.
struct AlertBannerEntry {
    IModule* module = nullptr;
    QString  vesselId;
    QString  vesselName;
    int      count = 0;
};

class AlertBanner : public QWidget
{
    Q_OBJECT
public:
    explicit AlertBanner(QWidget* parent = nullptr);

    // Rebuilds the row list from scratch and makes the banner visible.
    // Calling this with an empty list is a no-op — MainWindow only calls
    // it when there is something to show.
    void populate(const QList<AlertBannerEntry>& entries);

signals:
    void viewRequested(IModule* module, const QString& vesselId);
    void dismissed();

private:
    QVBoxLayout* m_rowsLayout = nullptr;
};
```

Layout: a slim frame with a heading ("Needs attention"), one row per entry
(vessel name, a short sentence built from `count` and the owning module's
`displayName()` lowercased for the sentence — e.g. "2 certificates need
attention" — and a **View** button), and a single dismiss control (✕) in
the corner that closes the whole banner and emits `dismissed()`. No cap on
row count for now (see §10) — fleet sizes this app targets keep this
short.

`MainWindow` places one `AlertBanner` above the existing sidebar/pages
`QSplitter`, inside a new top-level `QVBoxLayout` wrapping both (today
`setCentralWidget(splitter)` is called directly; it becomes
`setCentralWidget(wrapper)` where `wrapper` contains `[m_alertBanner,
splitter]`). Hidden by default; `populate()` is the only thing that shows
it.

## §8 Startup sequence and the drill-down

In `MainWindow`'s constructor, after `buildSidebar()` (and after the
optional `buildVesselSelector()`):

1. Build the combined entry list: for every module in `m_moduleSidebarRows`,
   for every `AlertProvider*` it reports, for every
   `VesselAttentionCount` that provider returns, append one
   `AlertBannerEntry{module, vesselId, vesselName, count}`.
2. Read `AppSettingRepository::read()`. Treat a failed read the same as an
   invalid `lastAlertToastDate` (never shown) — consistent with the
   fallback-to-defaults contract every other reader of this repository
   already follows.
3. If the combined list is non-empty **and** `lastAlertToastDate !=
   QDate::currentDate()`: call `m_alertBanner->populate(entries)`, then
   `appSettings.recordAlertToastShown(QDate::currentDate())`.
4. Call `refreshAlertBadges()` regardless of whether the banner showed —
   the sidebar count reflects the live situation even on a day the banner
   itself stays silent.

Wiring the banner's signals:

```cpp
connect(m_alertBanner, &AlertBanner::dismissed, m_alertBanner, &QWidget::hide);
connect(m_alertBanner, &AlertBanner::viewRequested, this, &MainWindow::showAttentionFor);
```

```cpp
void MainWindow::showAttentionFor(IModule* module, const QString& vesselId)
{
    const int row = m_moduleSidebarRows.value(module, -1);
    if (row < 0) {
        return;
    }

    // Set the filter before switching vessels, so only one reload()
    // happens instead of one for the vessel switch and a second for the
    // filter toggle.
    if (auto* certificates = qobject_cast<CertificateListWidget*>(m_pages->widget(row))) {
        certificates->setNeedsAttentionFilter(true);
    }

    // No selector at all in VESSEL mode — there is only ever one vessel,
    // and the widget above is already scoped to it.
    if (m_vesselSelector != nullptr) {
        const int index = m_vesselSelector->findData(vesselId);
        if (index >= 0) {
            m_vesselSelector->setCurrentIndex(index);
        }
    }

    m_sidebar->setCurrentRow(row);
}
```

This follows the same "one piece of direct wiring, generalise once a
second per-vessel module exists" precedent `MainWindow`'s own header
comment already documents for the vessel-selector-to-`CertificateListWidget`
link — `showAttentionFor()` is written generically (by `IModule*`, not by
name), so it does not itself need changing when a second module arrives;
only the `qobject_cast` line would need a sibling.

`CertificateListWidget` gains one new public method:

```cpp
// CertificateListWidget.h
void setNeedsAttentionFilter(bool on);
```

```cpp
// CertificateListWidget.cpp
void CertificateListWidget::setNeedsAttentionFilter(bool on)
{
    m_needsAttentionCheck->setChecked(on);
}
```

`QCheckBox::setChecked()` only emits `toggled` when the value actually
changes, so this is a no-op if the filter was already on — correct, since
`reload()` doesn't need to run twice.

## §9 Edge cases to test

1. Nothing needing attention anywhere → banner never shows; no badge
   suffix on the Certificates row.
2. Exactly one vessel outstanding, OFFICE mode → banner shows one row;
   View switches the selector to that vessel and applies the filter.
3. Multiple vessels outstanding → one row each; clicking one vessel's View
   does not remove or alter the other rows still showing in the banner.
4. VESSEL mode, something outstanding → banner shows exactly one row (this
   installation's own vessel); View applies the filter directly, no
   selector touched (`m_vesselSelector == nullptr`).
5. `lastAlertToastDate == today` at startup → banner does not appear even
   though there's something outstanding; the sidebar badge still reflects
   the live count regardless.
6. `lastAlertToastDate` is an earlier day and nothing is currently
   outstanding → banner doesn't show (nothing to show), and the date is
   left untouched — it wasn't actually shown, so tomorrow it's still
   eligible.
7. Dismissing the banner without clicking any View button still counts as
   shown for the day (recorded the moment it was populated, not on
   dismiss) — it does not reappear later in the same session.
8. `AppSettingRepository::read()` fails when computing thresholds →
   `CertificateAlertProvider` still runs, using the hardcoded 30/60/90
   defaults, the same fallback `CertificateListWidget::reload()` already
   uses.
9. Adding or editing a certificate (or endorsement) updates the sidebar
   badge count without restarting the app, via `certificatesChanged()` →
   `refreshAlertBadges()`. The already-shown banner, if still visible from
   startup, does **not** update its rows to match — a deliberate,
   documented simplification (see §10), not a defect.

## §10 Explicitly deferred

- A true fleet-wide report screen listing every outstanding certificate
  across every vessel in one table — belongs with Reports (step 14).
- Native OS toast notifications (`QSystemTrayIcon`) — behavior is
  inconsistent enough across Windows/macOS/Linux that an in-app banner is
  the more predictable choice for a cross-platform beginner project; can
  be reconsidered once each platform is actually being built and tested.
- Live re-evaluation across a midnight rollover in a long-running session —
  the once-a-day check happens once, at startup, not on a timer.
- The banner updating its own rows live after being shown — only the
  sidebar badge stays live within a session (§9 item 9); the banner is a
  startup snapshot until dismissed or the app is restarted.
- Row overflow/paging in the banner for a very large fleet — not built
  now; revisit if it becomes a real problem at actual fleet sizes.
- A second module's `AlertProvider` — the interface supports one, but only
  `CertificateAlertProvider` exists today; nothing exercises a multi-module
  banner yet.
