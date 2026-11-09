#include "modules/certificates/ui/CertificateListActions.h"

#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/CertificateAttachmentActions.h"
#include "modules/certificates/ui/CertificateDeleteActions.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateExtensionDialog.h"
#include "modules/certificates/ui/CertificateListRow.h"
#include "modules/certificates/ui/CertificateSelection.h"

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

    m_deleteActions = new CertificateDeleteActions(repository, endorsements, extensions,
                                                   attachments, table, dialogParent);

    connect(m_renewButton, &QPushButton::clicked, this,
            &CertificateListActions::renewCertificate);
    connect(m_extensionButton, &QPushButton::clicked, this,
            &CertificateListActions::startOrResolveExtension);

    // Forwarded rather than re-emitted by hand: both of these make the list
    // stale in exactly the same way this class's own actions do.
    connect(m_attachmentActions, &CertificateAttachmentActions::listChanged, this,
            &CertificateListActions::listChanged);
    connect(m_deleteActions, &CertificateDeleteActions::listChanged, this,
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
    return m_deleteActions->button();
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

QTableWidgetItem* CertificateListActions::selectedDataItem() const
{
    return CertificateSelection::singleDataItem(m_table);
}

QString CertificateListActions::selectedCertificateId() const
{
    return CertificateSelection::singleCertificateId(m_table);
}

QDate CertificateListActions::selectedExtensionExpiry() const
{
    return CertificateSelection::singleExtensionExpiry(m_table);
}

void CertificateListActions::updateButtonStates()
{
    updateRenewButtonState();
    updateExtensionButtonState();
    m_deleteActions->updateState();
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

