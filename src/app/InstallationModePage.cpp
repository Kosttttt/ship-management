#include "app/InstallationModePage.h"

#include "app/FirstRunWizard.h"

#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

InstallationModePage::InstallationModePage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle(tr("Installation type"));
    setSubTitle(tr("Choose what this copy of the application is. "
                   "This is asked once and cannot be changed afterwards."));

    m_officeButton = new QRadioButton(tr("&Office"), this);
    m_vesselButton = new QRadioButton(tr("&Vessel"), this);

    auto* officeHint = new QLabel(tr("This computer manages the whole fleet."), this);
    auto* vesselHint = new QLabel(tr("This computer belongs to one ship, and shows "
                                     "only that ship's data."),
                                  this);

    // Indent the explanations under their radio buttons.
    for (QLabel* hint : {officeHint, vesselHint}) {
        hint->setWordWrap(true);
        hint->setIndent(24);
        hint->setEnabled(false); // renders as secondary text
    }

    m_officeButton->setChecked(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_officeButton);
    layout->addWidget(officeHint);
    layout->addSpacing(12);
    layout->addWidget(m_vesselButton);
    layout->addWidget(vesselHint);
    layout->addStretch();

    // The Next/Finish button changes meaning with the choice, so the wizard
    // has to be told to re-evaluate which page comes next.
    connect(m_vesselButton, &QRadioButton::toggled, this, [this]() {
        emit completeChanged();
    });
}

InstallationMode InstallationModePage::selectedMode() const
{
    return m_vesselButton->isChecked() ? InstallationMode::Vessel : InstallationMode::Office;
}

int InstallationModePage::nextId() const
{
    if (m_vesselButton->isChecked()) {
        return static_cast<int>(WizardPageId::VesselIdentity);
    }
    return -1; // -1 means "this is the last page", so the button reads Finish
}
