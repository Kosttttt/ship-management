#include "modules/certificates/ui/CertificateEditDialog.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/ui/CertificateEditForm.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CertificateEditDialog::CertificateEditDialog(CertificateRepository&            repository,
                                             const QString&                    vesselId,
                                             const std::optional<Certificate>& existing,
                                             QWidget*                          parent)
    : QDialog(parent)
    , m_repository(repository)
    , m_isEditing(existing.has_value())
{
    setWindowTitle(m_isEditing ? tr("Edit Certificate") : tr("Add Certificate"));
    setModal(true);

    m_form = new CertificateEditForm(this);

    if (existing.has_value()) {
        m_form->setCertificate(*existing);
    } else {
        // Only the vessel is set: the form keeps its own defaults for
        // everything else, including an expiry date five years out rather
        // than "does not expire".
        m_form->setVesselId(vesselId);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(m_isEditing ? tr("Save") : tr("Add"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CertificateEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &CertificateEditDialog::reject);

    connect(m_form, &CertificateEditForm::validityChanged, m_okButton, &QPushButton::setEnabled);
    m_okButton->setEnabled(m_form->isValid());

    m_form->setFocusOnFirstField();
    resize(540, 620);
}

void CertificateEditDialog::accept()
{
    // Enter reaches QDialog::accept() directly even when OK is disabled — the
    // trap found in the first-run wizard in step 3.
    if (!m_form->isValid()) {
        return;
    }

    const Certificate edited = m_form->certificate();

    const bool saved = m_isEditing ? m_repository.update(edited) : m_repository.create(edited);

    if (!saved) {
        qWarning() << "Certificate save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    QDialog::accept();
}
