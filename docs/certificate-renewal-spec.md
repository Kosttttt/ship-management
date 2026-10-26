# Certificate renewal — implementation spec (step 10)

## §1 Purpose

Alerts (step 9) tell you a certificate needs attention. This step is what
you actually do about it: renew the certificate — update its dates (and
rules, if class changed them) — and start its next survey cycle with a
clean slate.

## §2 What "renewal" is, precisely (settled by design discussion)

Renewal edits the **same** `certificate` row in place. It does not create
a new certificate, and `certificate.previous_certificate_id` is not used —
there is no need for a "points back at the old one" link when there is no
new row to link from. This is simpler than an earlier draft of this spec
assumed, and it's worth recording clearly here so nothing later tries to
build around a link that was never needed.

Renewal is the final survey of a certificate's term — once every annual
and intermediate anniversary has been satisfied, the only thing left
outstanding is the renewal itself (already true today:
`computeCertificateState()` sets `nextSurveyType = SurveyType::Renewal`
once nothing else remains outstanding, and counts down to
`certificate.expiryDate`). This step is what happens once that renewal
survey has taken place: the certificate's dates move forward, and its
record of the completed cycle's surveys is cleared, since it doesn't
describe the new cycle.

**A worry raised and resolved during design, recorded for anyone reading
this later:** does keeping old endorsements attached to a renewed
certificate risk them being wrongly matched against the new cycle's survey
windows? No — `SurveySchedule::anniversariesOf()` computes anniversaries
fresh every time from whichever `expiryDate` the certificate currently
has (`expiryDate.addYears(-n)`); nothing is stored. The moment renewal
sets a new expiry date, the entire anniversary schedule regenerates from
scratch, and old endorsements — dated years before the new cycle's
earliest window even opens — would be silently skipped by
`evaluateAnnual()`'s existing "anything dated before this window opened
can never satisfy it" rule regardless. Clearing them is not a correctness
fix; it's simply what the developer wants (§4 below), since this module
tracks current certificates, not a historical archive.

## §3 Scope

**In scope:**
- A **Renew** button on the certificate list, enabled only when a
  certificate with a real expiry date is selected.
- A confirmation step naming how many recorded surveys will be cleared.
- Reusing `CertificateEditDialog` in a new **renewal mode**: the same
  fields, a different title, no Endorsements section, and a Save action
  that also clears the certificate's existing endorsements.
- A new `EndorsementRepository::deleteAllForCertificate()` method.

**Out of scope (see §8):**
- Attaching a new certificate document or archiving the old one — no file
  storage exists anywhere in this app yet. Per this design discussion,
  step 10 ships data-only; file attachment comes later, as its own
  properly-built module, and renewal gets extended to use it then.
- Any change to `SurveySchedule`/`computeCertificateState()` — both
  already anchor anniversaries to `expiryDate`, which is correct, and
  nothing here needs to touch them (see §2's worry, resolved).

## §4 What renewal does

Two things happen when a renewal is saved, in this order:

1. `CertificateRepository::update()` — unchanged, the same call an
   ordinary edit already makes. Renewal doesn't bypass validation: the
   no-expiry-with-survey-flags check, the strictly-valid-dates checks, all
   of it still applies to whatever the user entered.
2. `EndorsementRepository::deleteAllForCertificate(certificateId)` — new.
   Soft-deletes every endorsement recorded against this certificate
   (`is_deleted = 1`, same convention as every other write in this
   schema — CLAUDE.md §6 — never a hard `DELETE`).

**Deliberately not wrapped in one transaction.** Both are single,
unconditional `UPDATE` statements against a local SQLite file only this
app writes to; introducing cross-repository transaction coordination for
two single-table writes is more machinery than the actual risk justifies.
If step 2 somehow fails after step 1 already succeeded (very unlikely —
there is no realistic failure mode for an unconditional `UPDATE` on a
local file that step 1 didn't already rule out by succeeding), the
certificate is still correctly renewed; the error is reported clearly
rather than swallowed, and the practical consequence is bounded — per §2's
worked reasoning, stale endorsements left behind would just be ignored by
the next `computeCertificateState()` call anyway, since they'd predate the
new cycle's anniversaries. Flag it if it's ever actually seen; there's no
UI for a one-off manual cleanup today, since deleting an endorsement isn't
built anywhere in this app.

```cpp
// modules/certificates/data/EndorsementRepository.h — new method
bool deleteAllForCertificate(const QString& certificateId);
```

```cpp
// modules/certificates/data/EndorsementRepository.cpp
bool EndorsementRepository::deleteAllForCertificate(const QString& certificateId)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE endorsement SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE certificate_id = ? AND is_deleted = 0"));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(certificateId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not clear the certificate's recorded surveys:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true; // affecting zero rows (nothing to clear) is not a failure
}
```

## §5 The Renew button

`CertificateListWidget` gains a new button, `m_renewButton`, in the same
control row as `m_addButton`. Unlike Add, Renew targets a specific
certificate, so it is enabled only when the table has a selection *and*
that certificate has a real expiry date — renewing a certificate that
never expires (a Tonnage Certificate, typically) doesn't correspond to
anything in real life.

The "has an expiry" check rides on the row's existing id-carrying cell,
the same pattern `kCertificateIdRole` already uses:

```cpp
// fillRow() — one more data role alongside the existing certificate id
constexpr int kNeverExpiresRole = Qt::UserRole + 2;
numberItem->setData(kNeverExpiresRole, certificate.neverExpires());
```

```cpp
// New: called from QTableWidget::itemSelectionChanged, and once after
// every reload() (since reload() can clear the selection).
void CertificateListWidget::updateRenewButtonState()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        m_renewButton->setEnabled(false);
        return;
    }
    QTableWidgetItem* numberItem = m_table->item(row, static_cast<int>(Column::ListNumber));
    m_renewButton->setEnabled(numberItem != nullptr
                              && !numberItem->data(kNeverExpiresRole).toBool());
}
```

Clicking Renew (only reachable when enabled, so a selection and a real
expiry date are both already guaranteed):

```cpp
void CertificateListWidget::renewCertificate()
{
    const QString id = /* same lookup editCertificate() already uses */;

    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
        QMessageBox::warning(this, tr("Could not open certificate"), m_repository.errorString());
        return;
    }

    QList<Endorsement> endorsements;
    m_endorsements.list(id, &endorsements); // best-effort count for the message below

    const auto answer = QMessageBox::question(
        this, tr("Renew certificate"),
        endorsements.isEmpty()
            ? tr("Renew '%1'?").arg(certificate->name)
            : tr("Renewing '%1' will clear its %2 recorded survey(s). Continue?")
                  .arg(certificate->name)
                  .arg(endorsements.size()));
    if (answer != QMessageBox::Yes) {
        return;
    }

    CertificateEditDialog dialog(m_repository, m_endorsements, m_vesselId, certificate,
                                 /*isRenewal=*/true, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}
```

Reachable only via this button — double-clicking a row still opens the
ordinary, non-destructive Edit path, unchanged. Renewal's effect on survey
history is deliberate and confirmed; an ordinary edit must never carry
that side effect by accident.

## §6 `CertificateEditDialog`'s renewal mode

A new trailing constructor parameter, `bool isRenewal = false`, defaulting
to today's behaviour so every existing call site (`addCertificate()`,
`editCertificate()`) is unaffected. When `true`:

- **Title** reads something like "Renew — %1" instead of "Edit
  Certificate — %1", so the two modes are never visually ambiguous.
- **The Endorsements section is not shown.** Adding a new survey stamp to
  a certificate mid-renewal doesn't belong to either cycle: not the old
  one, which is about to be cleared, and not the new one, which has no
  anniversaries to satisfy until the new dates are actually saved.
- **All other fields behave exactly as an ordinary edit** — pre-filled
  from the certificate's current values, the same validation, the same
  Cancel behaviour (nothing happens at all).
- **Save**, on success, calls `EndorsementRepository::deleteAllForCertificate()`
  immediately after `CertificateRepository::update()` succeeds (§4)
  before calling `accept()`.

## §7 Edge cases to test

1. A successful renewal updates the certificate's dates (and rules, if
   changed) and leaves `EndorsementRepository::list()` for that
   certificate empty afterward.
2. Canceling the confirmation dialog leaves the certificate and its
   endorsements completely untouched — the edit dialog never opens.
3. Canceling the edit dialog itself (after confirming) also leaves
   everything untouched.
4. The Renew button is disabled with no row selected.
5. The Renew button is disabled for a selected certificate with no expiry
   date (`neverExpires() == true`).
6. The confirmation message correctly reads "Renew '%1'?" (no survey
   count) for a certificate with zero recorded endorsements, and names
   the actual count otherwise.
7. The Endorsements section does not appear anywhere in the dialog when
   opened via Renew, even for a certificate that currently has recorded
   endorsements.
8. After a successful renewal, `computeCertificateState()` for that
   certificate reflects the fresh cycle: seed a certificate close to due
   with endorsements recorded, renew it with new dates far out, and
   confirm the certificate list now shows `DisplayStatus::Valid` with
   nothing outstanding — an end-to-end check that the whole chain (new
   dates in, endorsements cleared, state recomputed) actually works
   together, not just each piece in isolation.
9. Double-clicking a row still opens the ordinary edit dialog — confirm
   its title, its Endorsements section, and its Save behaviour are all
   unchanged from before this step.

## §8 Explicitly deferred

- Attaching a new certificate document at renewal and archiving the old
  one — no file storage exists anywhere in this app; comes later as its
  own module, with renewal extended to use it once it does.
- A dedicated "renewal history" record (who renewed what, when, from
  which dates to which) — not requested; the developer's stated intent is
  tracking current certificates going forward, not maintaining an archive
  of past cycles.
- Any change to how `previous_certificate_id` is used — it remains
  unused, as it has been since step 5, since renewal doesn't create a new
  row to link from.
