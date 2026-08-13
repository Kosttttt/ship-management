#include "app/VesselIdentityPage.h"

#include "app/ImoNumberMessages.h"
#include "core/ImoNumberValidator.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

VesselIdentityPage::VesselIdentityPage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle(tr("Vessel identity"));
    setSubTitle(tr("Identify the ship this installation belongs to. "
                   "The remaining particulars are entered later."));

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("MV Example"));

    m_imoEdit = new QLineEdit(this);
    m_imoEdit->setPlaceholderText(tr("9074729"));

    m_imoError = new QLabel(this);
    m_imoError->setWordWrap(true);
    // Styled by role rather than a hard-coded colour, so it still reads
    // correctly under a dark theme.
    QPalette errorPalette = m_imoError->palette();
    errorPalette.setColor(QPalette::WindowText, QColor(0xC0, 0x39, 0x2B));
    m_imoError->setPalette(errorPalette);

    auto* form = new QFormLayout;
    form->addRow(tr("Vessel &name:"), m_nameEdit);
    form->addRow(tr("&IMO number:"), m_imoEdit);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_imoError);
    layout->addStretch();

    // Live validation: the spec asks for feedback as the user types, not on
    // Finish, so a wrong digit is caught at the moment it is entered.
    connect(m_imoEdit, &QLineEdit::textChanged, this, [this]() {
        updateImoFeedback();
        emit completeChanged();
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { emit completeChanged(); });

    updateImoFeedback();
}

void VesselIdentityPage::updateImoFeedback()
{
    // An untouched field should not be scolded before anything is typed.
    if (m_imoEdit->text().isEmpty()) {
        m_imoError->clear();
        return;
    }
    m_imoError->setText(ImoNumberMessages::describe(ImoNumberValidator::validate(m_imoEdit->text())));
}

QString VesselIdentityPage::vesselName() const
{
    return m_nameEdit->text().trimmed();
}

QString VesselIdentityPage::imoNumber() const
{
    return ImoNumberValidator::digitsOnly(m_imoEdit->text());
}

bool VesselIdentityPage::isComplete() const
{
    return !vesselName().isEmpty() && ImoNumberValidator::isValid(m_imoEdit->text());
}
