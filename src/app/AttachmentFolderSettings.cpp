#include "app/AttachmentFolderSettings.h"

#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

// Where the folder chooser opens: the user's home folder rather than Documents.
// A folder that synchronises itself — the case §2.1 is written for — is as
// likely to sit beside Documents as inside it.
QString startingFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}

// The same QFileDialog::getExistingDirectory() mechanism Attach Folder uses
// (§2.6), worded for this job instead: here the folder is a destination the
// application will write into, not a document being filed.
QString chooseVesselFolder(QWidget* parent, const QString& vesselName)
{
    return QFileDialog::getExistingDirectory(
        parent,
        AttachmentFolderSettings::tr("Choose the attachment folder for %1").arg(vesselName),
        startingFolder(), QFileDialog::ShowDirsOnly);
}

} // namespace

AttachmentFolderSettings::AttachmentFolderSettings(VesselRepository& vessels, QWidget* parent)
    : QWidget(parent)
    , m_vessels(vessels)
    , m_roots(vessels)
{
    setObjectName(QStringLiteral("attachmentFolderSettings"));

    auto* heading = new QLabel(tr("Attachment folders"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* explanation = new QLabel(
        tr("Where each vessel's attached documents are kept on this computer. The database "
           "itself is not affected and does not move. Nothing can be attached to a vessel's "
           "certificates until its folder has been set."),
        this);
    explanation->setWordWrap(true);
    explanation->setEnabled(false); // renders as secondary text

    m_rows = new QFormLayout;

    m_emptyLabel = new QLabel(tr("No vessels have been added yet."), this);
    m_emptyLabel->setObjectName(QStringLiteral("noVesselsForAttachments"));
    m_emptyLabel->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading);
    layout->addWidget(explanation);
    layout->addSpacing(4);
    layout->addLayout(m_rows);
    layout->addWidget(m_emptyLabel);

    reload();
}

int AttachmentFolderSettings::vesselRowCount() const
{
    return m_vesselCount;
}

void AttachmentFolderSettings::reload()
{
    // QFormLayout holds a label and a field per row; both have to go, or the
    // rows accumulate every time a folder is chosen.
    while (m_rows->rowCount() > 0) {
        m_rows->removeRow(0);
    }
    m_vesselCount = 0;

    QList<Vessel> vessels;
    if (!m_vessels.list(&vessels)) {
        qWarning() << "Could not read the fleet for the attachment folders:"
                   << m_vessels.errorString();
        m_emptyLabel->setText(tr("The vessel list could not be read."));
        m_emptyLabel->setVisible(true);
        return;
    }

    m_emptyLabel->setText(tr("No vessels have been added yet."));
    m_emptyLabel->setVisible(vessels.isEmpty());

    for (const Vessel& vessel : vessels) {
        // Shown in native separators: this is a path the user chose themselves
        // and will recognise, unlike the relative paths stored against a
        // certificate, which are never displayed.
        auto* pathLabel = new QLabel(
            vessel.attachmentRootPath.isEmpty()
                ? tr("Not set — nothing can be attached to this vessel yet")
                : QDir::toNativeSeparators(vessel.attachmentRootPath),
            this);
        pathLabel->setObjectName(QStringLiteral("attachmentFolderPath_%1").arg(vessel.id));
        pathLabel->setWordWrap(true);
        pathLabel->setEnabled(!vessel.attachmentRootPath.isEmpty());

        auto* chooseButton = new QPushButton(tr("Choose…"), this);
        chooseButton->setObjectName(QStringLiteral("attachmentFolderButton_%1").arg(vessel.id));

        const QString vesselId   = vessel.id;
        const QString vesselName = vessel.name;
        connect(chooseButton, &QPushButton::clicked, this,
                [this, vesselId, vesselName]() { chooseFolderFor(vesselId, vesselName); });

        auto* row = new QHBoxLayout;
        row->addWidget(pathLabel, 1);
        row->addWidget(chooseButton, 0);

        m_rows->addRow(tr("%1:").arg(vessel.name), row);
        ++m_vesselCount;
    }
}

void AttachmentFolderSettings::chooseFolderFor(const QString& vesselId, const QString& vesselName)
{
    const QString chosen = chooseVesselFolder(this, vesselName);
    if (chosen.isEmpty()) {
        return; // cancelled: change nothing
    }

    QString error;
    if (!m_roots.changeRoot(vesselId, chosen, &error)) {
        qWarning() << "Could not set the attachment folder for" << vesselName << ":" << error;
        QMessageBox::warning(this, tr("Could not use this folder"),
                             tr("Nothing has been changed or moved.\n\n%1").arg(error));
        return;
    }

    qInfo() << "Attachment folder for" << vesselName << "set to" << chosen;

    // Read back rather than trusting what was chosen: the stored row is the
    // truth, and changeRoot() may have stored a tidied-up form of the path.
    reload();
}
