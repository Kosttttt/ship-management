# Certificate short-term extension — implementation spec (step 11)

## §1 Purpose

Sometimes class or flag issues a genuine short-term certificate — a
separate document, valid for a short window — while some problem with the
real certificate's cycle gets sorted out. The original certificate is not
touched: once the problem is resolved, it simply continues being the valid
certificate it always was. This step gives the app a way to represent that
bridge period without disturbing anything about the original certificate.

This is **not** a second kind of renewal. Renewal (step 10) edits the
certificate's own row in place and starts a fresh cycle. An extension
changes nothing about the certificate itself — it is a separate, small
record that temporarily takes over what the list shows and alerts on,
and disappears (resolved, not deleted-forever) once the problem is sorted.

## §2 What this is, precisely

- A certificate has at most one **active** extension at a time — a row in
  a new `certificate_extension` table with its own expiry date and an
  optional note.
- The certificate's own `issue_date`/`expiry_date` are never read from or
  written to by anything in this step. They sit there completely
  unaffected the whole time.
- While an extension is active, the list shows the **extension's** expiry
  date, not the certificate's, and a short visual tag so it's obvious
  something unusual is going on. `computeCertificateState()` computes
  severity from the extension's date using the exact same thresholds it
  already uses for a certificate's own expiry — no new severity tier is
  needed.
- Survey-window evaluation is suppressed while an extension is active: a
  short-term certificate is essentially never long enough to have its own
  annual/intermediate cycle, so there is nothing there to evaluate, and
  evaluating the *original* certificate's windows during a window that
  doesn't correspond to anything happening right now would show numbers
  that mean nothing to the person reading them. Use exactly the same
  "nothing to evaluate" path `computeCertificateState()` already takes for
  a certificate whose survey flags aren't set — no new `SurveySeverity`
  value should be needed either.
- Resolving an extension **soft-deletes** it — same convention as
  everywhere else in this schema. Unlike endorsements on renewal, this
  history is worth keeping: it's a real document from an outside
  authority, not an internal survey stamp. No screen browses that history
  yet (§9), but the data must survive.
- Starting a second extension while one is already active is refused with
  a friendly message, the same "defensive repository" pattern used
  everywhere else — not allowed by a UI affordance in the first place
  (§7), but the repository is the backstop regardless of what calls it.
- **Renew is disabled while an extension is active** (my call — flag it if
  this is wrong). Renewing clears survey history and starts a fresh
  cycle; that doesn't fit while the real problem is still open.

## §3 Scope

New files:
- `migrations/007_create_certificate_extension.sql`
- `modules/certificates/domain/CertificateExtension.h` (plain struct, no
  `.cpp` needed — same shape as `Endorsement`)
- `modules/certificates/data/CertificateExtensionRepository.h/.cpp`
- `modules/certificates/ui/CertificateExtensionDialog.h/.cpp` — a small
  dialog, shaped like `EndorsementEditDialog`: one date field, one
  optional note field, OK/Cancel.
- `tests/modules/certificates/tst_CertificateExtension.cpp`

Changed files:
- `modules/certificates/domain/CertificateState.h/.cpp` — new optional
  parameter and a new passthrough field (§6).
- `modules/certificates/ui/CertificateListWidget.h/.cpp` — new button,
  new data role, `fillRow()` changes, Renew gating (§7).
- `modules/certificates/CertificateAlertProvider.cpp` — **must** also
  look up each certificate's active extension and pass it into
  `computeCertificateState()`, the same as `CertificateListWidget::reload()`
  does. Missing this would mean the sidebar badge and daily banner still
  alert on a certificate's paused survey schedule during an active
  extension, which is exactly the kind of gap step 6's own verification
  caught once already — double-check both call sites got updated, not
  just one.
- `modules/certificates/CertificatesModule` — gains a
  `CertificateExtensionRepository` member, threaded into both
  `CertificateListWidget` and `CertificateAlertProvider`.
- `CMakeLists.txt` and `tests/CMakeLists.txt` — new source files, new
  test target, `ship_add_migrations()` for the new test.

Out of scope: any UI for browsing past/resolved extensions (§9); any file
attachment (still nothing built for file storage anywhere); any change to
`CertificateEditDialog` or the Renew flow itself beyond the one gating
check named above.

## §4 Data model

```sql
-- 007_create_certificate_extension.sql
CREATE TABLE certificate_extension (
    id             TEXT    PRIMARY KEY NOT NULL,
    certificate_id TEXT    NOT NULL,
    expiry_date    TEXT    NOT NULL,
    note           TEXT,
    created_at     TEXT    NOT NULL,
    created_by     TEXT    NOT NULL,
    updated_at     TEXT    NOT NULL,
    updated_by     TEXT    NOT NULL,
    is_deleted     INTEGER NOT NULL DEFAULT 0,
    origin_node    TEXT    NOT NULL,
    revision       INTEGER NOT NULL DEFAULT 1
);
```

No foreign key to `certificate` — same trade-off as `endorsement`, resolved
the same way for consistency (every repository already re-validates scope
and existence itself). `expiry_date` is `NOT NULL`: unlike a certificate's
own expiry, a short-term certificate without an expiry date isn't a
meaningful concept, so there is no "does not expire" case here to support.

```cpp
// modules/certificates/domain/CertificateExtension.h
#pragma once

#include <QDate>
#include <QString>

struct CertificateExtension
{
    QString id;
    QString certificateId;
    QDate   expiryDate;
    QString note; // optional; may be empty
};
```

## §5 `CertificateExtensionRepository`

Mirrors `EndorsementRepository`'s shape: scope is applied transitively
through the parent certificate, the same `loadParentCertificate()`-style
lookup.

```cpp
// modules/certificates/data/CertificateExtensionRepository.h
#pragma once

#include "modules/certificates/domain/CertificateExtension.h"

#include <QString>
#include <optional>

class InstallationContext;
class QSqlDatabase;

class CertificateExtensionRepository
{
public:
    CertificateExtensionRepository(QSqlDatabase& database, const InstallationContext& installation);

    // The one active (not-yet-resolved) extension for a certificate, if
    // any. Another vessel's certificate returns nothing rather than an
    // error, the established "not found is a successful answer" precedent.
    bool findActiveFor(const QString& certificateId, std::optional<CertificateExtension>* extension);

    // Refuses with a friendly message if this certificate already has an
    // active extension, or if expiryDate is invalid — this is a write.
    bool start(const CertificateExtension& extension, QString* newId = nullptr);

    // Soft-deletes the active extension for this certificate, if any.
    // Affecting zero rows is success, not failure — nothing to resolve is
    // a normal case (the UI shouldn't offer this with nothing active, but
    // the repository doesn't rely on the UI to enforce that).
    bool resolve(const QString& certificateId);

    QString errorString() const;

private:
    bool validate(const CertificateExtension& extension);
    QString vesselIdForCertificate(const QString& certificateId, bool* ok);

    QSqlDatabase&              m_database;
    const InstallationContext& m_installation;
    QString                    m_errorString;
};
```

`start()`'s validation order (friendly message before any raw constraint):
1. The parent certificate must exist and be in scope (VESSEL mode).
2. `expiryDate` must be valid: *"An extension needs its own expiry date."*
3. No existing active extension for this certificate: *"This certificate
   already has an active short-term extension. Resolve it before starting
   another."*

## §6 `CertificateState` integration

```cpp
// modules/certificates/domain/CertificateState.h — signature change
CertificateState computeCertificateState(
    const Certificate&        certificate,
    const QList<Endorsement>& endorsements,
    const AlertThresholds&    thresholds,
    const QDate&              today,
    const std::optional<QDate>& activeExtensionExpiry = std::nullopt);
```

Passing just the date, not the whole `CertificateExtension` — this keeps
the domain function's dependencies exactly as narrow as they were, the
same reasoning that already kept `AlertThresholds` a separate plain
struct. The caller (list widget, alert provider) is the one that knows
how to look an active extension up; this function just needs the one date.

Behavior when `activeExtensionExpiry` has a value:
- Expiry severity is computed from `*activeExtensionExpiry` instead of
  `certificate.expiryDate`, through the exact same threshold logic already
  used for a certificate's own expiry.
- Survey severity takes whatever path already exists for "no survey
  tracked" (the same one a certificate with no survey requirement already
  takes) — no new enum value needed on either axis. If this isn't a clean
  fit against the actual existing enum shapes, flag it in the report
  rather than inventing a new `DisplayStatus` value without asking.
- `CertificateState` gains one new field, a plain passthrough:
  `std::optional<QDate> activeExtensionExpiry;` — set directly from the
  parameter of the same name, so the UI can tell whether a row is under an
  active extension and what date to show, without a second lookup.
- `reason` notes the extension is active and until when.

When the parameter is omitted (every existing call site, until §3's
changed files are updated), behavior is identical to today — this must be
a pure addition, not a change to any already-passing test.

## §7 UI

**The button.** `CertificateListWidget` gains a button next to Renew,
object name `certificateExtensionButton`. Its label and action depend on
whether the selected certificate currently has an active extension
(known from `CertificateState::activeExtensionExpiry` on the row, the
same way `kNeverExpiresRole` already rides on the row today — add a
parallel data role rather than querying the repository again on every
selection change):

- No active extension: label **"Start Short-Term Extension…"**. Opens
  `CertificateExtensionDialog` (date + optional note). Accepting calls
  `CertificateExtensionRepository::start()` and reloads.
- An active extension: label **"Short-Term Active — Resolve"**. Clicking
  calls `resolve()` directly and reloads — no confirmation dialog, since
  nothing is being destroyed that can't be re-started if it was a mistake.

Disabled with no selection, and disabled for a certificate with no expiry
date (`kNeverExpiresRole`) — same rule Renew already applies, since a
short-term extension to something that never expires isn't a real concept
either.

**Renew gating.** `updateRenewButtonState()` also disables Renew when the
selected row has an active extension (`activeExtensionExpiry` set).

**List display.** `fillRow()`: when `state.activeExtensionExpiry` has a
value, the Expiry Date cell shows that date with a short suffix —
`"12 Mar 2027 — Short Term"` — instead of the certificate's own expiry
label. No new column: this keeps the existing seven-column layout and
every already-built sort/filter behavior untouched.

## §8 Edge cases to test

1. Starting an extension on a certificate with none active succeeds; the
   list immediately shows the extension's date with the "Short Term"
   suffix.
2. Starting a second extension while one is already active is refused
   with the friendly message, nothing changes.
3. The extension button is disabled for a certificate with no expiry date.
4. Resolving an active extension soft-deletes it; the list reverts to
   showing the certificate's own original expiry date and status,
   computed exactly as it would have been if the extension had never
   existed.
5. While an extension is active, a certificate that would otherwise read
   Survey Overdue (unsatisfied anniversaries on its own cycle) instead
   reads according to the extension's own date — survey evaluation is
   genuinely suppressed, not just visually overridden.
6. Renew is disabled while an extension is active, and re-enabled once it
   is resolved.
7. A resolved extension's row still exists in the database afterward
   (soft-deleted, not gone) — queryable directly even with no UI to browse
   it.
8. VESSEL-mode scope: starting or resolving an extension against another
   vessel's certificate is refused the same way `EndorsementRepository`
   already enforces this transitively.
9. The sidebar badge / alert banner (via `CertificateAlertProvider`) does
   not count a certificate as needing attention for a stale survey window
   while its extension is active, confirming both call sites described in
   §3 were actually updated, not just the list widget's.

## §9 Explicitly deferred

- A screen for browsing a certificate's past (resolved) extensions. The
  data is preserved specifically so this can be added later without a
  migration; nothing in the UI shows it yet.
- Any file attachment to an extension record (no file storage exists
  anywhere in the app yet — same deferral as step 10).
- Whether an extension should ever need its own survey tracking. Not
  while they're assumed to always be too short-lived for that to matter;
  revisit only if that assumption turns out to be wrong in practice.
