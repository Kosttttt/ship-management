#include "modules/certificates/ui/CertificateExtensionDialog.h"

#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/domain/CertificateExtension.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/AttachmentSourceField.h"

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// A bridge period, not a term: three months is a plausible starting point the
// user will usually overwrite with the date on the actual document. Reading
// the clock here is fine — this is the UI layer, and the pure state
// calculation still never does it (certificate-control-spec §4).
constexpr int kDefaultMonths = 3;

} // namespace

CertificateExtensionDialog::CertificateExtensionDialog(
    CertificateExtensionRepository& repository,
    CertificateAttachmentService&   attachments,
    const Certificate&              certificate,
    QWidget*                        parent)
    : QDialog(parent)
    , m_repository(repository)
    , m_attachments(attachments)
    , m_certificate(certificate)
{
    setObjectName(QStringLiteral("certificateExtensionDialog"));
    setWindowTitle(tr("Start Short-Term Extension"));
    setModal(true);

    // Naming the certificate, because this dialog is reached from a selection
    // in a list and nothing else on it would say which row was selected.
    auto* heading =
        new QLabel(tr("Short-term extension for '%1'.").arg(m_certificate.name), this);
    heading->setWordWrap(true);

    m_expiryEdit = new QDateEdit(this);
    m_expiryEdit->setObjectName(QStringLiteral("extensionExpiryEdit"));
    m_expiryEdit->setCalendarPopup(true);
    m_expiryEdit->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    m_expiryEdit->setDate(QDate::currentDate().addMonths(kDefaultMonths));

    m_noteEdit = new QLineEdit(this);
    m_noteEdit->setObjectName(QStringLiteral("extensionNoteEdit"));
    m_noteEdit->setPlaceholderText(tr("Optional — e.g. why it was issued"));

    // certificate-attachment-spec §8: the short-term certificate's own
    // paperwork, which is a different document from the certificate's and is
    // never archived — the extension row is its own historical record (§2).
    m_attachmentSource = new AttachmentSourceField(tr("No document attached."), this);

    auto* form = new QFormLayout;
    form->addRow(tr("Extension expires"), m_expiryEdit);
    form->addRow(tr("Note"), m_noteEdit);
    form->addRow(tr("Document"), m_attachmentSource);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Start Extension"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CertificateExtensionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &CertificateExtensionDialog::reject);

    // No validityChanged wiring, unlike EndorsementEditDialog: a QDateEdit
    // cannot hold an invalid date, and the note is optional, so OK can never
    // be the wrong thing to offer here.

    m_expiryEdit->setFocus();
    resize(420, sizeHint().height());
}

void CertificateExtensionDialog::accept()
{
    CertificateExtension extension;
    extension.certificateId = m_certificate.id;
    extension.expiryDate    = m_expiryEdit->date();
    extension.note          = m_noteEdit->text();

    // The file is copied in before the row is inserted, which is the order
    // certificate-attachment-spec §7 requires: the path has to be known to be
    // stored on the row. A failure here stops the whole thing, because unlike
    // the renewal case nothing has been written yet — so this is still the
    // "correct it and try again" state the dialog is built around.
    if (!m_attachmentSource->sourcePath().isEmpty()) {
        QString relativePath;
        if (!m_attachments.addExtensionDocument(m_certificate, m_expiryEdit->date(),
                                                m_attachmentSource->sourcePath(),
                                                m_attachmentSource->sourceIsFolder(),
                                                &relativePath)) {
            qWarning() << "Extension document could not be stored:"
                       << m_attachments.errorString();
            QMessageBox::warning(this, tr("Could not attach the document"),
                                 tr("Nothing has been recorded, so you can correct this and try "
                                    "again.\n\n%1")
                                     .arg(m_attachments.errorString()));
            return; // stay open, data intact
        }
        extension.attachmentPath = relativePath;
    }

    // The repository is the one that refuses a second active extension (§5),
    // so the dialog stays open on failure with everything typed still intact
    // — the same shape EndorsementEditDialog uses for a rejected save.
    if (!m_repository.start(extension)) {
        qWarning() << "Short-term extension could not be started:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not start the extension"),
                             tr("Nothing has been recorded, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    QDialog::accept();
}
