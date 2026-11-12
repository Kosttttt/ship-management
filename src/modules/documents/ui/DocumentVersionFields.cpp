#include "modules/documents/ui/DocumentVersionFields.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

DocumentVersionFields::DocumentVersionFields(QWidget* parent)
    : QWidget(parent)
{
    m_fileLabel = new QLabel(tr("No file chosen yet."), this);
    m_fileLabel->setObjectName(QStringLiteral("documentFileLabel"));
    m_fileLabel->setWordWrap(true);
    m_fileLabel->setEnabled(false); // renders as secondary text

    auto* chooseButton = new QPushButton(tr("&Choose File…"), this);
    chooseButton->setObjectName(QStringLiteral("chooseDocumentFileButton"));

    auto* fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->addWidget(m_fileLabel, 1);
    fileRow->addWidget(chooseButton, 0);

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setObjectName(QStringLiteral("revisionLabelEdit"));
    m_labelEdit->setPlaceholderText(tr("Optional — e.g. Rev 5"));

    // Off by default, and the date only becomes editable when it is ticked:
    // a QDateEdit always holds *some* date, and storing today's because
    // nobody said otherwise would be inventing information (§4.1).
    m_dateCheck = new QCheckBox(tr("Revision date"), this);
    m_dateCheck->setObjectName(QStringLiteral("revisionDateCheck"));

    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setObjectName(QStringLiteral("revisionDateEdit"));
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    m_dateEdit->setEnabled(false);

    auto* dateRow = new QHBoxLayout;
    dateRow->setContentsMargins(0, 0, 0, 0);
    dateRow->addWidget(m_dateCheck);
    dateRow->addWidget(m_dateEdit, 1);

    m_noteEdit = new QLineEdit(this);
    m_noteEdit->setObjectName(QStringLiteral("changeNoteEdit"));
    m_noteEdit->setPlaceholderText(tr("Optional — what changed"));

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow(tr("File:"), fileRow);
    layout->addRow(tr("Revision:"), m_labelEdit);
    layout->addRow(QString(), dateRow);
    layout->addRow(tr("What changed:"), m_noteEdit);

    connect(chooseButton, &QPushButton::clicked, this, &DocumentVersionFields::chooseFile);
    connect(m_dateCheck, &QCheckBox::toggled, m_dateEdit, &QDateEdit::setEnabled);
}

void DocumentVersionFields::chooseFile()
{
    // No file-type filter: whatever the office or the ship already has is
    // what gets filed — a PDF, a Word document, a scan, a spreadsheet. The
    // same judgment certificate attachments made.
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Choose the document file"));
    if (chosen.isEmpty()) {
        return; // cancelled: leave any earlier choice exactly as it was
    }

    m_sourcePath = chosen;
    // Native separators because this is a path the user picked and will
    // recognise — unlike the relative paths stored in the database, which are
    // never displayed.
    m_fileLabel->setText(QDir::toNativeSeparators(chosen));
    m_fileLabel->setEnabled(true);

    emit fileChosen();
}

QString DocumentVersionFields::sourcePath() const
{
    return m_sourcePath;
}

DocumentVersion DocumentVersionFields::values() const
{
    DocumentVersion version;
    version.revisionLabel = m_labelEdit->text().trimmed();
    version.revisionDate  = m_dateCheck->isChecked() ? m_dateEdit->date() : QDate();
    version.changeNote    = m_noteEdit->text().trimmed();
    // What the file is called now, kept so the Versions list can show
    // something a person recognises rather than the stored name.
    version.originalFileName = QFileInfo(m_sourcePath).fileName();
    return version;
}
