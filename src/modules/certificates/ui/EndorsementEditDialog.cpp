#include "modules/certificates/ui/EndorsementEditDialog.h"

#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/EndorsementEditForm.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

EndorsementEditDialog::EndorsementEditDialog(EndorsementRepository&            repository,
                                             const QString&                    certificateId,
                                             const QList<SurveyType>&          allowedTypes,
                                             const std::optional<Endorsement>& existing,
                                             QWidget*                          parent)
    : QDialog(parent)
    , m_repository(repository)
    , m_isEditing(existing.has_value())
{
    setObjectName(QStringLiteral("endorsementEditDialog"));
    setWindowTitle(m_isEditing ? tr("Edit Endorsement") : tr("Add Endorsement"));
    setModal(true);

    m_form = new EndorsementEditForm(certificateId, allowedTypes, this);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(m_isEditing ? tr("Save") : tr("Add"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &EndorsementEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EndorsementEditDialog::reject);

    connect(m_form, &EndorsementEditForm::validityChanged, m_okButton, &QPushButton::setEnabled);

    // After the signal is wired, so that pre-filling the form also settles the
    // OK button rather than leaving it at whatever an empty form implied.
    if (m_isEditing) {
        m_form->setEndorsement(*existing);
    }
    m_okButton->setEnabled(m_form->isValid());

    m_form->setFocusOnFirstField();
    resize(420, sizeHint().height());
}

void EndorsementEditDialog::accept()
{
    // Enter reaches QDialog::accept() directly even when OK is disabled — the
    // trap found in the first-run wizard in step 3.
    if (!m_form->isValid()) {
        return;
    }

    const Endorsement endorsement = m_form->endorsement();
    const bool        saved =
        m_isEditing ? m_repository.update(endorsement) : m_repository.create(endorsement);

    if (!saved) {
        qWarning() << "Endorsement save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    QDialog::accept();
}
