#include "modules/documents/ui/DocumentVersionDialog.h"

#include "modules/documents/ui/DocumentVersionFields.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

DocumentVersionDialog::DocumentVersionDialog(const QString& documentTitle, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add New Version — %1").arg(documentTitle));
    setModal(true);
    setObjectName(QStringLiteral("documentVersionDialog"));

    auto* explanation =
        new QLabel(tr("The new version becomes the current one. The version it replaces is "
                      "kept and stays openable from Versions."),
                   this);
    explanation->setWordWrap(true);
    explanation->setEnabled(false); // renders as secondary text

    m_fields = new DocumentVersionFields(this);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Add Version"));
    // Nothing to add until a file is chosen: a version is a file, and the
    // other three fields are all optional.
    m_okButton->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(explanation);
    layout->addSpacing(8);
    layout->addWidget(m_fields);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_fields, &DocumentVersionFields::fileChosen, this,
            [this] { m_okButton->setEnabled(!m_fields->sourcePath().isEmpty()); });

    resize(520, 280);
}

QString DocumentVersionDialog::sourcePath() const
{
    return m_fields->sourcePath();
}

DocumentVersion DocumentVersionDialog::values() const
{
    return m_fields->values();
}
