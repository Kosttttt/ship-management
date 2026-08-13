#include "app/VesselEditForm.h"

#include "app/ImoNumberMessages.h"
#include "core/ImoNumberValidator.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// Comfortably above the largest ship afloat, and small enough that a stray
// keystroke cannot produce a nonsense figure.
constexpr int kMaximumGrossTonnage = 1000000;

} // namespace

VesselEditForm::VesselEditForm(QWidget* parent)
    : QWidget(parent)
{
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("MV Example"));

    m_imoEdit = new QLineEdit(this);
    m_imoEdit->setPlaceholderText(tr("9074729"));

    m_imoError = new QLabel(this);
    m_imoError->setWordWrap(true);
    QPalette errorPalette = m_imoError->palette();
    errorPalette.setColor(QPalette::WindowText, QColor(0xC0, 0x39, 0x2B));
    m_imoError->setPalette(errorPalette);

    m_callSignEdit = new QLineEdit(this);

    // A QSpinBox cannot hold a negative number or a fraction, so the rule in
    // vessel-crud-spec §5 is enforced by the choice of widget rather than by
    // checking text afterwards. 0 displays as "Not entered" (spec §3).
    m_grossTonnageSpin = new QSpinBox(this);
    m_grossTonnageSpin->setRange(0, kMaximumGrossTonnage);
    m_grossTonnageSpin->setSpecialValueText(tr("Not entered"));
    m_grossTonnageSpin->setGroupSeparatorShown(true);

    m_portOfRegistryEdit = new QLineEdit(this);
    m_flagStateEdit      = new QLineEdit(this);

    auto* form = new QFormLayout;
    form->addRow(tr("Vessel &name:"), m_nameEdit);
    form->addRow(tr("&IMO number:"), m_imoEdit);
    form->addRow(QString(), m_imoError);
    form->addRow(tr("&Call sign:"), m_callSignEdit);
    form->addRow(tr("&Gross tonnage:"), m_grossTonnageSpin);
    form->addRow(tr("&Port of registry:"), m_portOfRegistryEdit);
    form->addRow(tr("&Flag state:"), m_flagStateEdit);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(form);

    // Live validation, the same pattern the first-run wizard established: a
    // wrong digit is reported as it is typed, not on Save.
    connect(m_imoEdit, &QLineEdit::textChanged, this, [this]() {
        updateImoFeedback();
        announceValidity();
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { announceValidity(); });

    updateImoFeedback();
}

void VesselEditForm::updateImoFeedback()
{
    // An untouched field should not be scolded before anything is typed.
    if (m_imoEdit->text().isEmpty()) {
        m_imoError->clear();
        return;
    }
    m_imoError->setText(ImoNumberMessages::describe(ImoNumberValidator::validate(m_imoEdit->text())));
}

void VesselEditForm::announceValidity()
{
    emit validityChanged(isValid());
}

void VesselEditForm::setVessel(const Vessel& vessel)
{
    m_id = vessel.id;
    m_nameEdit->setText(vessel.name);
    m_imoEdit->setText(vessel.imoNumber);
    m_callSignEdit->setText(vessel.callSign);
    m_grossTonnageSpin->setValue(vessel.grossTonnage);
    m_portOfRegistryEdit->setText(vessel.portOfRegistry);
    m_flagStateEdit->setText(vessel.flagState);

    updateImoFeedback();
    announceValidity();
}

Vessel VesselEditForm::vessel() const
{
    Vessel vessel;
    vessel.id             = m_id;
    vessel.name           = m_nameEdit->text().trimmed();
    vessel.imoNumber      = ImoNumberValidator::digitsOnly(m_imoEdit->text());
    vessel.callSign       = m_callSignEdit->text().trimmed();
    vessel.grossTonnage   = m_grossTonnageSpin->value();
    vessel.portOfRegistry = m_portOfRegistryEdit->text().trimmed();
    vessel.flagState      = m_flagStateEdit->text().trimmed();
    return vessel;
}

bool VesselEditForm::isValid() const
{
    // Uniqueness is deliberately not checked here: it needs a database query,
    // and running one per keystroke would be wasteful. It is checked on Save.
    return !m_nameEdit->text().trimmed().isEmpty()
           && ImoNumberValidator::isValid(m_imoEdit->text());
}

void VesselEditForm::setFocusOnFirstField()
{
    m_nameEdit->setFocus();
}
