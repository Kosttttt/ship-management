#include "app/DocumentsFolderSettings.h"

#include "core/DocumentsFolder.h"

#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

DocumentsFolderSettings::DocumentsFolderSettings(QWidget* parent)
    : QWidget(parent)
{
    auto* group = new QGroupBox(tr("Documents"), this);
    group->setObjectName(QStringLiteral("documentsFolderGroup"));

    auto* explanation =
        new QLabel(tr("Where the Documents module keeps its files. Every version of every "
                      "document is a separate file in this folder, filed under the fleet or "
                      "the vessel it belongs to."),
                   group);
    explanation->setWordWrap(true);
    explanation->setEnabled(false); // renders as secondary text

    m_folderLabel = new QLabel(QString(), group);
    m_folderLabel->setObjectName(QStringLiteral("documentsFolderLabel"));
    m_folderLabel->setWordWrap(true);

    auto* chooseButton = new QPushButton(tr("C&hoose…"), group);
    chooseButton->setObjectName(QStringLiteral("chooseDocumentsFolderButton"));

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->addWidget(m_folderLabel, 1);
    folderRow->addWidget(chooseButton, 0);

    auto* form = new QFormLayout;
    form->addRow(tr("Folder:"), folderRow);

    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(explanation);
    groupLayout->addLayout(form);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group);

    // Read before anything is connected, so building this widget only ever
    // reads the settings — the same care BackupSettings' section takes.
    reload();

    connect(chooseButton, &QPushButton::clicked, this, &DocumentsFolderSettings::chooseFolder);
}

void DocumentsFolderSettings::reload()
{
    const QString folder = DocumentsFolder::path();

    m_folderLabel->setText(folder.isEmpty() ? tr("No folder chosen yet.")
                                            : QDir::toNativeSeparators(folder));
    // Greyed while there is none, which is how every other "not set yet"
    // state on this page reads.
    m_folderLabel->setEnabled(!folder.isEmpty());
}

void DocumentsFolderSettings::chooseFolder()
{
    const QString current = DocumentsFolder::path();
    const QString chosen  = QFileDialog::getExistingDirectory(
        this, tr("Choose the documents folder"), current, QFileDialog::ShowDirsOnly);

    if (chosen.isEmpty()) {
        return; // cancelled: leave the stored folder exactly as it was
    }
    if (QDir::cleanPath(chosen) == QDir::cleanPath(current)) {
        return; // the same folder: nothing to say
    }

    const bool hadOne = !current.isEmpty();
    DocumentsFolder::setPath(chosen);
    reload();

    if (!hadOne) {
        return; // nothing was anywhere else, so there is nothing to explain
    }

    // §4.5: changing the folder does not move what is already filed, and
    // saying so now is far better than someone finding an empty list later.
    QMessageBox::information(
        this, tr("Documents folder changed"),
        tr("New documents will be filed here.\n\nFiles already in the previous folder are "
           "left where they are — nothing was moved. If you want them here, copy them "
           "across yourself:\n\n%1")
            .arg(QDir::toNativeSeparators(current)));
}
