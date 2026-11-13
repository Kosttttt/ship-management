#include "modules/documents/ui/DocumentSpacePanel.h"

#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentSpaceRepository.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

constexpr int kSpaceIdRole = Qt::UserRole + 1;

// Name and kind in one small dialog, because a space's kind cannot be
// changed afterwards — a manual that became a collection would have
// revisions that mean nothing — so it has to be chosen with the name.
bool askForNewSpace(QWidget* parent, QString* name, SpaceKind* kind)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(DocumentSpacePanel::tr("New Space"));
    dialog.setModal(true);

    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setObjectName(QStringLiteral("newSpaceNameEdit"));

    auto* kindCombo = new QComboBox(&dialog);
    kindCombo->setObjectName(QStringLiteral("newSpaceKindCombo"));
    kindCombo->addItem(DocumentSpacePanel::tr("Manual — the whole space gets a revision"),
                       static_cast<int>(SpaceKind::Manual));
    kindCombo->addItem(DocumentSpacePanel::tr("Collection — each page is published on its own"),
                       static_cast<int>(SpaceKind::Collection));

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton* okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(false);

    auto* form = new QFormLayout;
    form->addRow(DocumentSpacePanel::tr("&Name:"), nameEdit);
    form->addRow(DocumentSpacePanel::tr("&Kind:"), kindCombo);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(nameEdit, &QLineEdit::textChanged, okButton,
                     [okButton](const QString& text) {
                         okButton->setEnabled(!text.trimmed().isEmpty());
                     });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    *name = nameEdit->text().trimmed();
    *kind = static_cast<SpaceKind>(kindCombo->currentData().toInt());
    return true;
}

} // namespace

DocumentSpacePanel::DocumentSpacePanel(DocumentSpaceRepository& spaces,
                                       DocumentPageRemovals& removals, QWidget* parent)
    : QWidget(parent)
    , m_spaces(spaces)
    , m_removals(removals)
{
    auto* heading = new QLabel(tr("SPACES"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("documentSpaceList"));

    m_newButton = new QPushButton(tr("&New Space…"), this);
    m_newButton->setObjectName(QStringLiteral("newSpaceButton"));
    m_renameButton = new QPushButton(tr("&Rename…"), this);
    m_renameButton->setObjectName(QStringLiteral("renameSpaceButton"));
    m_deleteButton = new QPushButton(tr("De&lete…"), this);
    m_deleteButton->setObjectName(QStringLiteral("deleteSpaceButton"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(m_newButton);
    buttonRow->addWidget(m_renameButton);
    buttonRow->addWidget(m_deleteButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttonRow);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) {
        updateButtons();
        emit spaceSelected(selectedSpace().id);
    });
    connect(m_newButton, &QPushButton::clicked, this, &DocumentSpacePanel::createSpace);
    connect(m_renameButton, &QPushButton::clicked, this, &DocumentSpacePanel::renameSpace);
    connect(m_deleteButton, &QPushButton::clicked, this, &DocumentSpacePanel::deleteSpace);
}

void DocumentSpacePanel::reload()
{
    const QString previous = selectedSpace().id;

    m_loaded.clear();
    if (!m_spaces.list(&m_loaded)) {
        QMessageBox::warning(this, tr("Could not read the spaces"), m_spaces.errorString());
        return;
    }

    // Signals blocked while the rows are rebuilt, or currentRowChanged fires
    // for every intermediate state and the screen reloads a page per row.
    const QSignalBlocker blocker(m_list);
    m_list->clear();

    int restored = -1;
    for (int index = 0; index < m_loaded.size(); ++index) {
        const DocumentSpace& space = m_loaded.at(index);

        auto* item = new QListWidgetItem(space.name, m_list);
        item->setData(kSpaceIdRole, space.id);
        // A manual and a collection behave differently when published, so the
        // list says which is which rather than leaving it to be discovered.
        item->setToolTip(space.isManual() ? tr("Manual — published as a whole revision")
                                          : tr("Collection — pages published one at a time"));
        if (space.id == previous) {
            restored = index;
        }
    }

    // By id, so adding or removing a space does not move the selection onto
    // a different one.
    m_list->setCurrentRow(restored >= 0 ? restored : (m_loaded.isEmpty() ? -1 : 0));

    updateButtons();
    emit spaceSelected(selectedSpace().id);
}

DocumentSpace DocumentSpacePanel::selectedSpace() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (item == nullptr) {
        return DocumentSpace();
    }
    const QString id = item->data(kSpaceIdRole).toString();
    for (const DocumentSpace& space : m_loaded) {
        if (space.id == id) {
            return space;
        }
    }
    return DocumentSpace();
}

void DocumentSpacePanel::updateButtons()
{
    const DocumentSpace space = selectedSpace();
    const bool          hasOne = !space.id.isEmpty();
    const bool          mayEdit = hasOne && m_spaces.mayEdit(space);

    // §4.3: a vessel installation creates no company spaces, and its own
    // space is made for it — so New Space is the office's button.
    m_newButton->setEnabled(m_spaces.mayEdit(DocumentSpace()));

    m_renameButton->setEnabled(mayEdit);
    // A vessel's own space cannot be deleted: it belongs to the vessel, and
    // an archived vessel's space is hidden rather than removed (§4.3).
    m_deleteButton->setEnabled(mayEdit && !space.isVesselSpace());
}

void DocumentSpacePanel::createSpace()
{
    QString   name;
    SpaceKind kind = SpaceKind::Unset;
    if (!askForNewSpace(this, &name, &kind)) {
        return;
    }

    if (!m_spaces.createCompanySpace(name, kind, QString(), nullptr)) {
        QMessageBox::warning(this, tr("Could not create the space"), m_spaces.errorString());
        return;
    }
    reload();
}

void DocumentSpacePanel::renameSpace()
{
    const DocumentSpace space = selectedSpace();
    if (space.id.isEmpty()) {
        return;
    }

    bool          accepted = false;
    const QString name     = QInputDialog::getText(this, tr("Rename space"), tr("Name:"),
                                                   QLineEdit::Normal, space.name, &accepted);
    if (!accepted || name.trimmed().isEmpty() || name.trimmed() == space.name) {
        return;
    }

    if (!m_spaces.rename(space.id, name)) {
        QMessageBox::warning(this, tr("Could not rename the space"), m_spaces.errorString());
        return;
    }
    reload();
}

void DocumentSpacePanel::deleteSpace()
{
    const DocumentSpace space = selectedSpace();
    if (space.id.isEmpty()) {
        return;
    }

    int pageCount = 0;
    m_removals.countSpacePages(space.id, &pageCount);

    // §5: an empty space goes without ceremony; one with pages says how many
    // go with it, because "delete space" reads much smaller than it is.
    QMessageBox confirmation(this);
    confirmation.setIcon(QMessageBox::Question);
    confirmation.setWindowTitle(tr("Delete space?"));
    confirmation.setText(tr("Delete '%1'?").arg(space.name));
    confirmation.setInformativeText(
        pageCount == 0
            ? tr("It has no pages.")
            : tr("Its %n page(s) go with it. Nothing is destroyed — a deleted space and its "
                 "pages stay in the database and can be brought back by hand.",
                 nullptr, pageCount));
    confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmation.setDefaultButton(QMessageBox::No);

    if (confirmation.exec() != QMessageBox::Yes) {
        return;
    }

    // The pages first: a space that vanished with its pages still live would
    // leave them unreachable rather than deleted.
    if (!m_removals.softDeleteSpacePages(space.id)) {
        QMessageBox::warning(this, tr("Could not delete the space"), m_removals.errorString());
        return;
    }
    if (!m_spaces.softDelete(space.id)) {
        QMessageBox::warning(this, tr("Could not delete the space"), m_spaces.errorString());
        return;
    }
    reload();
}
