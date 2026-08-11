#include "app/FirstRunWizard.h"

#include "app/InstallationModePage.h"
#include "app/VesselIdentityPage.h"
#include "core/InstallationRepository.h"

#include <QMessageBox>

FirstRunWizard::FirstRunWizard(InstallationRepository& repository, QWidget* parent)
    : QWizard(parent)
    , m_repository(repository)
{
    setWindowTitle(tr("Ship Management System — Setup"));

    m_modePage     = new InstallationModePage(this);
    m_identityPage = new VesselIdentityPage(this);

    setPage(static_cast<int>(WizardPageId::Mode), m_modePage);
    setPage(static_cast<int>(WizardPageId::VesselIdentity), m_identityPage);
    setStartId(static_cast<int>(WizardPageId::Mode));

    // Cancel is a legitimate answer — the application simply exits — so the
    // button stays. There is no Back-to-nothing state to guard against.
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setWizardStyle(QWizard::ModernStyle);
}

void FirstRunWizard::accept()
{
    // Pressing Enter reaches QDialog::accept() directly from any page, whatever
    // the buttons say, so this cannot assume it was reached by clicking a
    // Finish button that was enabled. Both conditions are checked here.
    const QWizardPage* page = currentPage();
    if (page == nullptr || !page->isComplete()) {
        return; // required fields still missing or invalid
    }
    if (page->nextId() != -1) {
        next(); // not the last page: Enter should move forward, not finish
        return;
    }

    const InstallationMode mode = m_modePage->selectedMode();

    bool               written = false;
    InstallationRecord created;

    if (mode == InstallationMode::Vessel) {
        written = m_repository.createVesselInstallation(m_identityPage->vesselName(),
                                                        m_identityPage->imoNumber(),
                                                        &created);
    } else {
        written = m_repository.createOfficeInstallation(&created);
    }

    if (!written) {
        qCritical() << "First-run setup failed:" << m_repository.errorString();
        QMessageBox::critical(this,
                              tr("Setup could not be saved"),
                              tr("Nothing has been written, so you can correct this and "
                                 "try again.\n\n%1")
                                  .arg(m_repository.errorString()));
        return; // stay open
    }

    m_record = created;
    qInfo() << "Installation configured as" << m_record.nodeId;

    QWizard::accept();
}

InstallationRecord FirstRunWizard::record() const
{
    return m_record;
}
