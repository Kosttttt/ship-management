#include "app/VesselDetailPage.h"

#include "app/VesselEditForm.h"
#include "core/InstallationContext.h"
#include "core/VesselRepository.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

VesselDetailPage::VesselDetailPage(VesselRepository&          repository,
                                   const InstallationContext& installation,
                                   QWidget*                   parent)
    : QWidget(parent)
    , m_repository(repository)
    , m_installation(installation)
{
    auto* heading = new QLabel(tr("Vessel particulars"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    m_form = new VesselEditForm(this);

    m_saveButton      = new QPushButton(tr("&Save"), this);
    auto* discardButton = new QPushButton(tr("&Discard changes"), this);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setEnabled(false); // renders as secondary text

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_saveButton);
    buttonRow->addWidget(discardButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_statusLabel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addSpacing(8);
    layout->addWidget(m_form);
    layout->addSpacing(8);
    layout->addLayout(buttonRow);
    layout->addStretch();

    connect(m_saveButton, &QPushButton::clicked, this, &VesselDetailPage::save);
    connect(discardButton, &QPushButton::clicked, this, &VesselDetailPage::reload);
    connect(m_form, &VesselEditForm::validityChanged, m_saveButton, &QPushButton::setEnabled);

    reload();
}

void VesselDetailPage::reload()
{
    std::optional<Vessel> vessel;
    if (!m_repository.findById(m_installation.vesselId(), &vessel)) {
        qWarning() << "Could not load this vessel:" << m_repository.errorString();
        QMessageBox::warning(this, tr("Could not load vessel"), m_repository.errorString());
        return;
    }

    if (!vessel.has_value()) {
        // Only reachable if the row were removed behind the application's
        // back; the wizard guarantees it exists at first run.
        m_statusLabel->setText(tr("This installation's vessel is missing from the database."));
        m_saveButton->setEnabled(false);
        return;
    }

    m_form->setVessel(*vessel);
    m_saveButton->setEnabled(m_form->isValid());
    m_statusLabel->clear();
}

void VesselDetailPage::save()
{
    if (!m_form->isValid()) {
        return;
    }

    if (!m_repository.update(m_form->vessel())) {
        qWarning() << "Vessel save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return;
    }

    qInfo() << "Vessel particulars saved.";
    m_statusLabel->setText(tr("Saved."));

    // Read back rather than trusting the form: the stored row is the truth,
    // and this also picks up the incremented revision.
    reload();
    m_statusLabel->setText(tr("Saved."));
}
