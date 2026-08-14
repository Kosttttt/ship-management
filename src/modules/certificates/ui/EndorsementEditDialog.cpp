#include "modules/certificates/ui/EndorsementEditDialog.h"

#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/EndorsementEditForm.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

EndorsementEditDialog::EndorsementEditDialog(EndorsementRepository&   repository,
                                             const QString&           certificateId,
                                             const QList<SurveyType>& allowedTypes,
                                             QWidget*                 parent)
    : QDialog(parent)
    , m_repository(repository)
{
    setWindowTitle(tr("Add Endorsement"));
    setModal(true);

    m_form = new EndorsementEditForm(certificateId, allowedTypes, this);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Add"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &EndorsementEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EndorsementEditDialog::reject);

    connect(m_form, &EndorsementEditForm::validityChanged, m_okButton, &QPushButton::setEnabled);
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

    if (!m_repository.create(m_form->endorsement())) {
        qWarning() << "Endorsement save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been recorded, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    QDialog::accept();
}
