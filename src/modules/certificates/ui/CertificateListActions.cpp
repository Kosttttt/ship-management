#include "modules/certificates/ui/CertificateListActions.h"

#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/CertificateAttachmentActions.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateExtensionDialog.h"
#include "modules/certificates/ui/CertificateListRow.h"

#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

#include <optional>

CertificateListActions::CertificateListActions(CertificateRepository&          repository,
                                               EndorsementRepository&          endorsements,
                                               CertificateExtensionRepository& extensions,
                                               CertificateAttachmentService&   attachments,
                                               QTableWidget*                   table,
                                               QWidget*                        dialogParent)
    : QObject(dialogParent)
    , m_repository(repository)
    , m_endorsements(endorsements)
    , m_extensions(extensions)
    , m_attachments(attachments)
    , m_deletion(repository, extensions, attachments)
    , m_table(table)
    , m_dialogParent(dialogParent)
{
    // Unlike Add, Renew targets one selected certificate — and only one that
    // actually expires (certificate-renewal-spec §5).
    m_renewButton = new QPushButton(tr("&Renew…"), dialogParent);
    m_renewButton->setObjectName(QStringLiteral("renewCertificateButton"));
    m_renewButton->setEnabled(false);

    // Both directions on one button (certificate-extension-spec §7). Its text
    // is set by updateExtensionButtonState(); the label here is only what it
    // reads while nothing is selected.
    m_extensionButton = new QPushButton(tr("Start Short-Term &Extension…"), dialogParent);
    m_extensionButton->setObjectName(QStringLiteral("certificateExtensionButton"));
    m_extensionButton->setEnabled(false);

    m_attachmentActions =
        new CertificateAttachmentActions(repository, attachments, table, dialogParent);

    // addendum 2 §4. The ellipsis is not decoration: it says a question comes
    // before anything happens, which on the one destructive button in the row
    // is worth saying in the label.
    m_deleteButton = new QPushButton(tr("&Delete…"), dialogParent);
    m_deleteButton->setObjectName(QStringLiteral("deleteCertificateButton"));
    m_deleteButton->setEnabled(false);

    connect(m_renewButton, &QPushButton::clicked, this,
            &CertificateListActions::renewCertificate);
    connect(m_extensionButton, &QPushButton::clicked, this,
            &CertificateListActions::startOrResolveExtension);
    connect(m_deleteButton, &QPushButton::clicked, this,
            &CertificateListActions::deleteCertificate);

    // Forwarded rather than re-emitted by hand: the attachment menu's actions
    // make the list stale in exactly the same way this class's do.
    connect(m_attachmentActions, &CertificateAttachmentActions::listChanged, this,
            &CertificateListActions::listChanged);

    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &CertificateListActions::updateButtonStates);
}

QPushButton* CertificateListActions::renewButton() const
{
    return m_renewButton;
}

QPushButton* CertificateListActions::extensionButton() const
{
    return m_extensionButton;
}

QPushButton* CertificateListActions::deleteButton() const
{
    return m_deleteButton;
}

QPushButton* CertificateListActions::attachmentButton() const
{
    return m_attachmentActions->button();
}

void CertificateListActions::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
    m_attachmentActions->setVesselId(vesselId);
}

int CertificateListActions::selectedRow() const
{
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    return selected.isEmpty() ? -1 : selected.first().row();
}

QTableWidgetItem* CertificateListActions::selectedDataItem() const
{
    const int row = selectedRow();
    return row < 0 ? nullptr : m_table->item(row, CertificateRow::kDataColumn);
}

QString CertificateListActions::selectedCertificateId() const
{
    QTableWidgetItem* item = selectedDataItem();
    return item == nullptr ? QString()
                           : item->data(CertificateRow::kCertificateIdRole).toString();
}

QDate CertificateListActions::selectedExtensionExpiry() const
{
    QTableWidgetItem* item = selectedDataItem();
    return item == nullptr ? QDate() : item->data(CertificateRow::kActiveExtensionRole).toDate();
}

void CertificateListActions::updateButtonStates()
{
    updateRenewButtonState();
    updateExtensionButtonState();
    updateDeleteButtonState();
    m_attachmentActions->updateState();
}

void CertificateListActions::updateRenewButtonState()
{
    QTableWidgetItem* item = selectedDataItem();

    // A certificate that never expires cannot be renewed — that corresponds
    // to nothing in real life (certificate-renewal-spec §5).
    if (item == nullptr || item->data(CertificateRow::kNeverExpiresRole).toBool()) {
        m_renewButton->setEnabled(false);
        return;
    }

    // certificate-extension-spec §7: not while a short-term extension is in
    // force. Renewing clears the survey history and starts a fresh cycle,
    // which does not fit while the problem the extension bridges is still
    // open.
    m_renewButton->setEnabled(!selectedExtensionExpiry().isValid());
}

void CertificateListActions::updateExtensionButtonState()
{
    QTableWidgetItem* item = selectedDataItem();

    // Same rule Renew applies: a short-term extension to something that never
    // expires is not a real concept either (certificate-extension-spec §7).
    const bool usable = item != nullptr && !item->data(CertificateRow::kNeverExpiresRole).toBool();
    m_extensionButton->setEnabled(usable);

    // The label carries the mode, so there is never a second button that is
    // disabled most of the time.
    const bool active = usable && selectedExtensionExpiry().isValid();
    m_extensionButton->setText(active ? tr("Short-Term Active — &Resolve")
                                      : tr("Start Short-Term &Extension…"));
}

void CertificateListActions::updateDeleteButtonState()
{
    // No never-expires rule and no extension rule, unlike the two above: any
    // certificate can be entered by mistake, including one that never expires
    // and one currently extended (§4).
    m_deleteButton->setEnabled(!selectedCertificateId().isEmpty());
}

void CertificateListActions::renewCertificate()
{
    const QString id = selectedCertificateId();
    if (id.isEmpty()) {
        return;
    }

    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
        QMessageBox::warning(m_dialogParent, tr("Could not open certificate"),
                             m_repository.errorString());
        return;
    }

    // Best-effort count for the message: an unreadable list should not block
    // a renewal, it just makes the question less specific.
    QList<Endorsement> endorsements;
    m_endorsements.list(id, &endorsements);

    // Naming what will be lost, because renewal is the one action here that
    // discards recorded data.
    const auto answer = QMessageBox::question(
        m_dialogParent, tr("Renew certificate"),
        endorsements.isEmpty()
            ? tr("Renew '%1'?").arg(certificate->name)
            : tr("Renewing '%1' will clear its %2 recorded survey(s). Continue?")
                  .arg(certificate->name)
                  .arg(endorsements.size()));

    if (answer != QMessageBox::Yes) {
        return; // nothing has happened at all
    }

    CertificateEditDialog dialog(m_repository, m_endorsements, m_attachments, m_vesselId,
                                 certificate, /*isRenewal=*/true, m_dialogParent);
    if (dialog.exec() == QDialog::Accepted) {
        emit listChanged();
    }
}

void CertificateListActions::startOrResolveExtension()
{
    const QString id = selectedCertificateId();
    if (id.isEmpty()) {
        return;
    }

    // Resolving: no confirmation dialog, because nothing is destroyed that
    // cannot simply be started again, and the row itself survives as history
    // (certificate-extension-spec §7).
    if (selectedExtensionExpiry().isValid()) {
        if (!m_extensions.resolve(id)) {
            QMessageBox::warning(m_dialogParent, tr("Could not resolve the extension"),
                                 m_extensions.errorString());
            return;
        }
        emit listChanged();
        return;
    }

    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
        QMessageBox::warning(m_dialogParent, tr("Could not open certificate"),
                             m_repository.errorString());
        return;
    }

    CertificateExtensionDialog dialog(m_extensions, m_attachments, *certificate, m_dialogParent);
    if (dialog.exec() == QDialog::Accepted) {
        emit listChanged();
    }
}

void CertificateListActions::deleteCertificate()
{
    const QString id = selectedCertificateId();
    if (id.isEmpty()) {
        return;
    }

    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
        QMessageBox::warning(m_dialogParent, tr("Could not open certificate"),
                             m_repository.errorString());
        return;
    }

    // Counted for the question, best-effort: an unreadable list makes the
    // wording less specific but must not block the deletion.
    QList<Endorsement> endorsements;
    m_endorsements.list(id, &endorsements);

    // §4: explicit that the real files go too, because that is the part
    // nothing else in this application does and the part that cannot be
    // undone from inside it. Named rather than described — "this certificate"
    // is not enough when the question arrives over a list of forty rows.
    QString message = tr("Delete '%1'?").arg(certificate->name);
    if (!endorsements.isEmpty()) {
        message += QLatin1Char('\n')
                   + tr("Its %n recorded survey(s) will be removed with it.", nullptr,
                        static_cast<int>(endorsements.size()));
    }
    message += QStringLiteral("\n\n")
               + tr("Any document attached to it, and any short-term extension's document, "
                    "will be permanently deleted from the vessel's attachment folder — not "
                    "moved to the archive. This cannot be undone from inside the "
                    "application.");

    const auto answer = QMessageBox::question(m_dialogParent, tr("Delete certificate"), message,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return; // nothing has happened at all
    }

    if (!m_deletion.deleteCertificate(*certificate)) {
        QMessageBox::warning(m_dialogParent, tr("Could not delete the certificate"),
                             m_deletion.errorString());
        return;
    }

    // Gone from the list, but a file would not go. Said out loud rather than
    // swallowed: somebody will otherwise find it in the folder later with no
    // certificate left to explain it.
    if (!m_deletion.errorString().isEmpty()) {
        QMessageBox::warning(
            m_dialogParent, tr("Deleted, but a file is still there"),
            tr("The certificate was deleted, but one of its files could not be "
               "removed.\n\n%1")
                .arg(m_deletion.errorString()));
    }

    emit listChanged();
}
