#include "modules/certificates/ui/AttachmentSourceField.h"

#include "modules/certificates/ui/AttachmentChooser.h"

#include <QAction>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>

AttachmentSourceField::AttachmentSourceField(const QString& emptyText, QWidget* parent)
    : QWidget(parent)
    , m_emptyText(emptyText)
{
    setObjectName(QStringLiteral("attachmentSourceField"));

    m_label = new QLabel(m_emptyText, this);
    m_label->setObjectName(QStringLiteral("attachmentSourceLabel"));
    m_label->setWordWrap(true);
    m_label->setEnabled(false); // renders as secondary text until something is chosen

    // The same one-button-with-a-menu shape the certificate list uses, so the
    // two ways of attaching a document look like the same feature.
    m_button = new QPushButton(tr("Choose…"), this);
    m_button->setObjectName(QStringLiteral("attachmentSourceButton"));

    auto* menu = new QMenu(m_button);

    QAction* fileAction = menu->addAction(tr("Attach &File…"));
    fileAction->setObjectName(QStringLiteral("attachmentSourceFileAction"));

    QAction* folderAction = menu->addAction(tr("Attach F&older…"));
    folderAction->setObjectName(QStringLiteral("attachmentSourceFolderAction"));

    m_button->setMenu(menu);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_button, 0);

    connect(fileAction, &QAction::triggered, this, [this]() { choose(/*folder=*/false); });
    connect(folderAction, &QAction::triggered, this, [this]() { choose(/*folder=*/true); });
}

QString AttachmentSourceField::sourcePath() const
{
    return m_sourcePath;
}

bool AttachmentSourceField::sourceIsFolder() const
{
    return m_sourceIsFolder;
}

void AttachmentSourceField::choose(bool folder)
{
    const QString chosen =
        folder ? AttachmentChooser::chooseFolder(this) : AttachmentChooser::chooseFile(this);

    if (chosen.isEmpty()) {
        return; // cancelled: leave any earlier choice exactly as it was
    }

    m_sourcePath     = chosen;
    m_sourceIsFolder = folder;

    // Shown in native separators because this is a path the user themselves
    // picked and will recognise — unlike the relative paths stored in the
    // database, which are never displayed.
    m_label->setText(QDir::toNativeSeparators(chosen));
    m_label->setEnabled(true);
}
