#include "app/VesselEditDialog.h"

#include "app/VesselEditForm.h"
#include "core/VesselRepository.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

VesselEditDialog::VesselEditDialog(VesselRepository&            repository,
                                   const std::optional<Vessel>& existing,
                                   QWidget*                     parent)
    : QDialog(parent)
    , m_repository(repository)
    , m_isEditing(existing.has_value())
{
    setWindowTitle(m_isEditing ? tr("Edit Vessel") : tr("Add Vessel"));
    setModal(true);

    m_form = new VesselEditForm(this);
    if (existing.has_value()) {
        m_form->setVessel(*existing);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(m_isEditing ? tr("Save") : tr("Add"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &VesselEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &VesselEditDialog::reject);

    // The button follows the form's own verdict rather than duplicating the
    // rules (CLAUDE.md §4 rule 3).
    connect(m_form, &VesselEditForm::validityChanged, m_okButton, &QPushButton::setEnabled);
    m_okButton->setEnabled(m_form->isValid());

    m_form->setFocusOnFirstField();
    resize(460, sizeHint().height());
}

void VesselEditDialog::accept()
{
    // Enter reaches QDialog::accept() directly even when OK is disabled — the
    // same trap found in the first-run wizard in step 3.
    if (!m_form->isValid()) {
        return;
    }

    const Vessel edited = m_form->vessel();

    const bool saved = m_isEditing ? m_repository.update(edited)
                                   : m_repository.create(edited);

    if (!saved) {
        qWarning() << "Vessel save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    QDialog::accept();
}
