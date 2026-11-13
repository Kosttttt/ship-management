#include "modules/documents/ui/DocumentPublishDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString describe(DocumentPublisher::PendingChange::Kind kind)
{
    switch (kind) {
    case DocumentPublisher::PendingChange::Kind::New:
        return DocumentPublishDialog::tr("new");
    case DocumentPublisher::PendingChange::Kind::Changed:
        return DocumentPublishDialog::tr("changed");
    case DocumentPublisher::PendingChange::Kind::Withdrawn:
        return DocumentPublishDialog::tr("withdrawn");
    }
    return QString();
}

} // namespace

DocumentPublishDialog::DocumentPublishDialog(const QString& heading, const QString& explanation,
                                             bool noteRequired, QWidget* parent)
    : QDialog(parent)
    , m_noteRequired(noteRequired)
{
    setWindowTitle(heading);
    setModal(true);
    setObjectName(QStringLiteral("documentPublishDialog"));

    auto* headingLabel = new QLabel(heading, this);
    QFont headingFont  = headingLabel->font();
    headingFont.setBold(true);
    headingLabel->setFont(headingFont);

    auto* explanationLabel = new QLabel(explanation, this);
    explanationLabel->setWordWrap(true);

    m_noteEdit = new QPlainTextEdit(this);
    m_noteEdit->setObjectName(QStringLiteral("publishNoteEdit"));
    m_noteEdit->setMaximumHeight(90);
    m_noteEdit->setPlaceholderText(noteRequired ? tr("What changed in this revision")
                                                : tr("Optional — what changed"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(headingLabel);
    layout->addWidget(explanationLabel);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(noteRequired ? tr("Note (required):") : tr("Note:"), this));
    layout->addWidget(m_noteEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Publish"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_noteEdit, &QPlainTextEdit::textChanged, this,
            &DocumentPublishDialog::updateOkState);

    updateOkState();
    resize(520, noteRequired ? 420 : 260);
}

void DocumentPublishDialog::updateOkState()
{
    // §6.2: a manual revision needs a note. The publisher refuses one
    // without, too; this is so nobody is told off after typing nothing.
    m_okButton->setEnabled(!m_noteRequired
                           || !m_noteEdit->toPlainText().trimmed().isEmpty());
}

void DocumentPublishDialog::buildChangeList(
    const QList<DocumentPublisher::PendingChange>& changes)
{
    auto* list = new QListWidget(this);
    list->setObjectName(QStringLiteral("publishChangeList"));
    list->setSelectionMode(QAbstractItemView::NoSelection);

    for (const DocumentPublisher::PendingChange& change : changes) {
        list->addItem(QStringLiteral("%1  —  %2").arg(change.title, describe(change.kind)));
    }

    // Above the note, because what is being published is what the note is
    // about: somebody writing the note wants the list in front of them.
    auto* boxLayout = qobject_cast<QVBoxLayout*>(layout());
    boxLayout->insertWidget(2, new QLabel(tr("This revision covers:"), this));
    boxLayout->insertWidget(3, list);
}

DocumentPublishDialog* DocumentPublishDialog::forPage(const QString& pageTitle, int nextNumber,
                                                      QWidget* parent)
{
    return new DocumentPublishDialog(
        tr("Publish page"),
        tr("'%1' will be published as revision %2. The version it replaces is kept.")
            .arg(pageTitle)
            .arg(nextNumber),
        /*noteRequired=*/false, parent);
}

DocumentPublishDialog* DocumentPublishDialog::forManual(
    const QString& spaceName, int nextNumber,
    const QList<DocumentPublisher::PendingChange>& changes, QWidget* parent)
{
    auto* dialog = new DocumentPublishDialog(
        tr("Publish revision"),
        tr("%1 will be published as Rev %2. Pages that have not changed keep the revision "
           "they are at.")
            .arg(spaceName)
            .arg(nextNumber),
        /*noteRequired=*/true, parent);

    dialog->buildChangeList(changes);
    return dialog;
}

QString DocumentPublishDialog::note() const
{
    return m_noteEdit->toPlainText().trimmed();
}
