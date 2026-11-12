#include "app/InstallationModePage.h"

#include "app/FirstRunWizard.h"
#include "app/RestoreFromBackup.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWizard>

InstallationModePage::InstallationModePage(QSqlDatabase& connection, const QString& dataFolder,
                                           QWidget* parent)
    : QWizardPage(parent)
    , m_connection(connection)
    , m_dataFolder(dataFolder)
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

    // §10.2: the way in for somebody moving to a new computer, who has no
    // installation to make and a backup to restore. Below the two choices and
    // separated from them, because it is not a third kind of installation —
    // it is "do not set this up at all, take it from a file".
    auto* restoreButton = new QPushButton(tr("&Restore from a backup instead…"), this);
    restoreButton->setObjectName(QStringLiteral("restoreFromBackupButton"));

    auto* restoreHint = new QLabel(tr("Moving to a new computer? Restore the other computer's "
                                      "backup and this copy becomes that installation."),
                                   this);
    restoreHint->setWordWrap(true);
    restoreHint->setEnabled(false); // renders as secondary text

    auto* restoreRow = new QHBoxLayout;
    restoreRow->setContentsMargins(0, 0, 0, 0);
    restoreRow->addWidget(restoreButton);
    restoreRow->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_officeButton);
    layout->addWidget(officeHint);
    layout->addSpacing(12);
    layout->addWidget(m_vesselButton);
    layout->addWidget(vesselHint);
    layout->addStretch();
    layout->addWidget(restoreHint);
    layout->addLayout(restoreRow);

    // The Next/Finish button changes meaning with the choice, so the wizard
    // has to be told to re-evaluate which page comes next.
    connect(m_vesselButton, &QRadioButton::toggled, this, [this]() {
        emit completeChanged();
    });
    connect(restoreButton, &QPushButton::clicked, this,
            &InstallationModePage::restoreFromBackup);
}

void InstallationModePage::restoreFromBackup()
{
    // showThisComputer is false: there is no installation yet, so there is
    // nothing to put in the confirmation's "this computer now" column (§10.2).
    if (RestoreFromBackup::run(this, m_connection, m_dataFolder, /*showThisComputer=*/false)
        != RestoreFromBackup::Outcome::Staged) {
        return; // cancelled, or a check refused the file: the wizard stays open
    }

    // Closed as cancelled, which is exactly right: no installation was
    // configured here. main() treats that as "the user declined to set the
    // application up" and exits without an error, and the copy already
    // starting does the swap before it opens the database.
    //
    // The empty database the program created before this wizard opened becomes
    // the before-restore- safety copy at the next start, which is harmless.
    if (wizard() != nullptr) {
        wizard()->reject();
    }
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
