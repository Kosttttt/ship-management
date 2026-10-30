#include "modules/certificates/ui/CertificateListActions.h"

#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/AttachmentChooser.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateExtensionDialog.h"
#include "modules/certificates/ui/CertificateListRow.h"

#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>

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

    // certificate-attachment-spec §8: one button, a menu of three entries, the
    // first of which is only there when something is actually attached. The
    // menu is parented to the button, so it is destroyed with it.
    m_attachmentButton = new QPushButton(tr("Atta&chment…"), dialogParent);
    m_attachmentButton->setObjectName(QStringLiteral("certificateAttachmentButton"));
    m_attachmentButton->setEnabled(false);

    auto* menu = new QMenu(m_attachmentButton);

    m_openAttachmentAction = menu->addAction(tr("&Open Current Attachment"));
    m_openAttachmentAction->setObjectName(QStringLiteral("openAttachmentAction"));
    menu->addSeparator();

    QAction* attachFileAction = menu->addAction(tr("Attach &File…"));
    attachFileAction->setObjectName(QStringLiteral("attachFileAction"));

    QAction* attachFolderAction = menu->addAction(tr("Attach F&older…"));
    attachFolderAction->setObjectName(QStringLiteral("attachFolderAction"));

    m_attachmentButton->setMenu(menu);

    connect(m_renewButton, &QPushButton::clicked, this,
            &CertificateListActions::renewCertificate);
    connect(m_extensionButton, &QPushButton::clicked, this,
            &CertificateListActions::startOrResolveExtension);
    connect(m_openAttachmentAction, &QAction::triggered, this,
            &CertificateListActions::openCurrentAttachment);
    connect(attachFileAction, &QAction::triggered, this, &CertificateListActions::attachFile);
    connect(attachFolderAction, &QAction::triggered, this, &CertificateListActions::attachFolder);
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

QPushButton* CertificateListActions::attachmentButton() const
{
    return m_attachmentButton;
}

void CertificateListActions::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
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
    updateAttachmentButtonState();
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

void CertificateListActions::updateAttachmentButtonState()
{
    const QString id = selectedCertificateId();
    // No never-expires rule here, unlike the other two buttons: a certificate
    // that never expires still has a real document behind it
    // (certificate-attachment-spec §8 disables this only with no selection).
    m_attachmentButton->setEnabled(!id.isEmpty());

    // Offered only when there is something to open, rather than shown greyed
    // out: "Open" on a certificate with no document is a question with no
    // useful answer. An unreadable row reports no path, so the entry hides —
    // the same "treat it as none" judgment the list already makes for an
    // unreadable endorsement or extension.
    m_openAttachmentAction->setVisible(!id.isEmpty()
                                       && !m_attachments.currentAbsolutePath(id).isEmpty());
}

void CertificateListActions::openCurrentAttachment()
{
    const QString id = selectedCertificateId();
    if (id.isEmpty()) {
        return;
    }

    const QString absolutePath = m_attachments.currentAbsolutePath(id);
    if (absolutePath.isEmpty()) {
        // Reachable if the attachment was replaced from elsewhere between the
        // menu opening and the entry being clicked.
        QMessageBox::information(m_dialogParent, tr("No document attached"),
                                 tr("This certificate has no attached document."));
        return;
    }

    // openUrl() handles a file and a folder identically, which is the whole
    // reason nothing above this line had to know which one it is (§8).
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))) {
        QMessageBox::warning(m_dialogParent, tr("Could not open the document"),
                             tr("Windows, macOS or your desktop refused to open:\n\n%1")
                                 .arg(QDir::toNativeSeparators(absolutePath)));
    }
}

void CertificateListActions::attachFile()
{
    attachFrom(AttachmentChooser::chooseFile(m_dialogParent), /*sourceIsFolder=*/false);
}

void CertificateListActions::attachFolder()
{
    attachFrom(AttachmentChooser::chooseFolder(m_dialogParent), /*sourceIsFolder=*/true);
}

void CertificateListActions::attachFrom(const QString& sourcePath, bool sourceIsFolder)
{
    const QString id = selectedCertificateId();
    if (id.isEmpty() || sourcePath.isEmpty()) {
        return; // nothing selected, or the chooser was cancelled
    }

    // The certificate itself, because its number, name and expiry date are
    // what the stored file is named after (§6).
    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
        QMessageBox::warning(m_dialogParent, tr("Could not open certificate"),
                             m_repository.errorString());
        return;
    }

    // The whole §7 sequence — archive, copy, record — lives in the service, so
    // that this screen and the renewal dialog cannot do it differently.
    if (!m_attachments.replaceCurrent(*certificate, certificate->expiryDate, sourcePath,
                                      sourceIsFolder)) {
        QMessageBox::warning(m_dialogParent, tr("Could not attach the document"),
                             m_attachments.errorString());
        return;
    }

    emit listChanged();
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
