#include "modules/certificates/ui/CertificateAttachmentActions.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/AttachmentChooser.h"
#include "modules/certificates/ui/CertificateSelection.h"

#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>

#include <optional>

CertificateAttachmentActions::CertificateAttachmentActions(
    CertificateRepository& repository, CertificateAttachmentService& attachments,
    QTableWidget* table, QWidget* dialogParent)
    : QObject(dialogParent)
    , m_repository(repository)
    , m_attachments(attachments)
    , m_table(table)
    , m_dialogParent(dialogParent)
{
    m_button = new QPushButton(tr("Atta&chment…"), dialogParent);
    m_button->setObjectName(QStringLiteral("certificateAttachmentButton"));
    m_button->setEnabled(false);

    // The menu is parented to the button, so it is destroyed with it.
    auto* menu = new QMenu(m_button);

    m_openAction = menu->addAction(tr("&Open Current Attachment"));
    m_openAction->setObjectName(QStringLiteral("openAttachmentAction"));
    menu->addSeparator();

    QAction* attachFileAction = menu->addAction(tr("Attach &File…"));
    attachFileAction->setObjectName(QStringLiteral("attachFileAction"));

    QAction* attachFolderAction = menu->addAction(tr("Attach F&older…"));
    attachFolderAction->setObjectName(QStringLiteral("attachFolderAction"));

    // Last, and behind a separator: addendum 2 §5. Shown under the same rule
    // as Open — only when there is actually a document — so it is never an
    // offer to delete nothing.
    menu->addSeparator();
    m_deleteAction = menu->addAction(tr("&Delete Attachment…"));
    m_deleteAction->setObjectName(QStringLiteral("deleteAttachmentAction"));

    m_button->setMenu(menu);

    connect(m_openAction, &QAction::triggered, this,
            &CertificateAttachmentActions::openCurrentAttachment);
    connect(attachFileAction, &QAction::triggered, this,
            &CertificateAttachmentActions::attachFile);
    connect(attachFolderAction, &QAction::triggered, this,
            &CertificateAttachmentActions::attachFolder);
    connect(m_deleteAction, &QAction::triggered, this,
            &CertificateAttachmentActions::deleteAttachment);
}

QPushButton* CertificateAttachmentActions::button() const
{
    return m_button;
}

void CertificateAttachmentActions::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
}

QString CertificateAttachmentActions::selectedCertificateId() const
{
    // Asks CertificateSelection rather than reading the selection itself, so
    // this button obeys the same multi-select rule as Renew and the extension
    // button: attaching a document to "the first of six selected rows" is not
    // a thing anyone means, so more than one selected reads as none
    // (certificate-list-usability-fixes.md §2). Before that change this class
    // kept its own copy of this lookup, which is exactly how it would have
    // been the one button left acting on a row the user had not singled out.
    return CertificateSelection::singleCertificateId(m_table);
}

void CertificateAttachmentActions::updateState()
{
    const QString id = selectedCertificateId();

    // No never-expires rule here, unlike Renew and the extension button: a
    // certificate that never expires still has a real document behind it
    // (§8 disables this only with no selection).
    m_button->setEnabled(!id.isEmpty());

    // Both offered only when there is something to act on, rather than shown
    // greyed out: "Open" on a certificate with no document is a question with
    // no useful answer, and "Delete" on one is an offer with nothing behind
    // it. An unreadable row reports no path, so both entries hide — the same
    // "treat it as none" judgment the list already makes for an unreadable
    // endorsement or extension.
    const bool hasAttachment =
        !id.isEmpty() && !m_attachments.currentAbsolutePath(m_vesselId, id).isEmpty();
    m_openAction->setVisible(hasAttachment);
    m_deleteAction->setVisible(hasAttachment);
}

void CertificateAttachmentActions::openCurrentAttachment()
{
    const QString id = selectedCertificateId();
    if (id.isEmpty()) {
        return;
    }

    const QString absolutePath = m_attachments.currentAbsolutePath(m_vesselId, id);
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

void CertificateAttachmentActions::attachFile()
{
    attachFrom(AttachmentChooser::chooseFile(m_dialogParent,
                                             m_attachments.vesselAttachmentRoot(m_vesselId)),
               /*sourceIsFolder=*/false);
}

void CertificateAttachmentActions::attachFolder()
{
    attachFrom(AttachmentChooser::chooseFolder(m_dialogParent,
                                               m_attachments.vesselAttachmentRoot(m_vesselId)),
               /*sourceIsFolder=*/true);
}

void CertificateAttachmentActions::attachFrom(const QString& sourcePath, bool sourceIsFolder)
{
    const QString id = selectedCertificateId();
    if (id.isEmpty() || sourcePath.isEmpty()) {
        return; // nothing selected, or the chooser was cancelled
    }

    // The certificate itself, because its number, name and dates are what the
    // stored file is named after (§6, and addendum 2 §7 for which date).
    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
        QMessageBox::warning(m_dialogParent, tr("Could not open certificate"),
                             m_repository.errorString());
        return;
    }

    // The whole §7 sequence — archive, copy, record — lives in the service, so
    // that this screen and the two dialogs cannot do it differently.
    if (!m_attachments.replaceCurrent(*certificate, sourcePath, sourceIsFolder)) {
        QMessageBox::warning(m_dialogParent, tr("Could not attach the document"),
                             m_attachments.errorString());
        return;
    }

    emit listChanged();
}

void CertificateAttachmentActions::deleteAttachment()
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

    // addendum 2 §5: lighter than deleting a certificate, but still a real
    // confirmation — the file goes for good, with no archive copy, which is
    // exactly what makes this different from replacing it.
    const auto answer = QMessageBox::question(
        m_dialogParent, tr("Delete attached document"),
        tr("Delete the document attached to '%1'?\n\n"
           "The file itself will be deleted from the vessel's attachment folder. "
           "Unlike replacing a document, no copy is kept in the archive, and this "
           "cannot be undone.")
            .arg(certificate->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return; // nothing has happened at all
    }

    if (!m_attachments.deleteCurrent(*certificate)) {
        QMessageBox::warning(m_dialogParent, tr("Could not delete the document"),
                             m_attachments.errorString());
        return;
    }

    // Deleted as far as the certificate is concerned, but the file would not
    // go. Said out loud rather than swallowed: somebody will otherwise find it
    // in the folder later and not know what it is.
    if (!m_attachments.errorString().isEmpty()) {
        QMessageBox::warning(
            m_dialogParent, tr("Removed, but the file is still there"),
            tr("The document is no longer attached to this certificate, but the file "
               "could not be deleted.\n\n%1")
                .arg(m_attachments.errorString()));
    }

    emit listChanged();
}
