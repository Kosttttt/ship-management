#include "app/VesselListWidget.h"

#include "app/VesselEditDialog.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QBrush>
#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// The vessel id is carried on the row's first cell so an edit knows which row
// it is editing without showing a UUID to the user.
constexpr int kVesselIdRole = Qt::UserRole + 1;

// Rides on the same cell as the id, for the same reason: the button and the
// double-click handler both need to know whether the selected row is archived,
// and neither should have to ask the database again to find out
// (vessel-archive-spec §7).
constexpr int kVesselArchivedRole = Qt::UserRole + 2;

enum class Column {
    Name      = 0,
    ImoNumber = 1,
    FlagState = 2,
    CallSign  = 3,
    Status    = 4
};

} // namespace

VesselListWidget::VesselListWidget(VesselRepository& repository, QWidget* parent)
    : QWidget(parent)
    , m_repository(repository)
{
    auto* addButton = new QPushButton(tr("&Add Vessel"), this);

    // One button whose label follows the selection, rather than an Archive and
    // a Restore sitting side by side with one of them always meaningless.
    m_actionButton = new QPushButton(tr("&Archive…"), this);
    m_actionButton->setEnabled(false);

    m_showArchived = new QCheckBox(tr("Show archived vessels"), this);
    m_showArchived->setChecked(false);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("IMO Number"), tr("Flag State"), tr("Call Sign"), tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // edited via the dialog
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->sortByColumn(static_cast<int>(Column::Name), Qt::AscendingOrder);

    auto* hint = new QLabel(tr("Double-click a vessel to edit it."), this);
    hint->setEnabled(false); // renders as secondary text

    auto* controls = new QHBoxLayout;
    controls->addWidget(addButton);
    controls->addWidget(m_actionButton);
    controls->addStretch();
    controls->addWidget(m_showArchived);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(m_table);
    layout->addWidget(hint);

    connect(addButton, &QPushButton::clicked, this, &VesselListWidget::addVessel);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &VesselListWidget::editVessel);
    connect(m_showArchived, &QCheckBox::toggled, this, &VesselListWidget::reload);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &VesselListWidget::updateActionButton);

    // Archive or Restore, decided by the row that is selected when it is
    // pressed — the button itself does not need to remember which it is.
    connect(m_actionButton, &QPushButton::clicked, this, [this] {
        QString vesselId;
        QString vesselName;
        bool    isArchived = false;
        if (!selectedVessel(&vesselId, &vesselName, &isArchived)) {
            return;
        }
        if (isArchived) {
            restoreSelected();
        } else {
            archiveSelected();
        }
    });

    reload();
}

void VesselListWidget::reload()
{
    // The checkbox is the single source of truth for this, so the window can
    // keep calling reload() with no argument exactly as it always has.
    const bool includeArchived = (m_showArchived != nullptr) && m_showArchived->isChecked();

    QList<Vessel> vessels;
    if (!m_repository.list(&vessels, includeArchived)) {
        qWarning() << "Could not load the vessel list:" << m_repository.errorString();
        QMessageBox::warning(this, tr("Could not load vessels"), m_repository.errorString());
        return;
    }

    // Sorting has to be off while rows are filled, or the table reorders
    // underneath the loop and cells land in the wrong rows.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(vessels.size());

    for (int row = 0; row < vessels.size(); ++row) {
        const Vessel& vessel = vessels.at(row);

        auto* nameItem = new QTableWidgetItem(vessel.name);
        nameItem->setData(kVesselIdRole, vessel.id);
        nameItem->setData(kVesselArchivedRole, vessel.isArchived);

        m_table->setItem(row, static_cast<int>(Column::Name), nameItem);
        m_table->setItem(row, static_cast<int>(Column::ImoNumber),
                         new QTableWidgetItem(vessel.imoNumber));
        m_table->setItem(row, static_cast<int>(Column::FlagState),
                         new QTableWidgetItem(vessel.flagState));
        m_table->setItem(row, static_cast<int>(Column::CallSign),
                         new QTableWidgetItem(vessel.callSign));
        m_table->setItem(row, static_cast<int>(Column::Status),
                         new QTableWidgetItem(vessel.isArchived ? tr("Archived")
                                                                : tr("Active")));

        if (!vessel.isArchived) {
            continue;
        }

        // Greyed and italic, NOT setEnabled(false) on the items: a disabled
        // item cannot be selected, and an archived row that cannot be
        // selected is a row whose Restore button can never be reached.
        QFont archivedFont = m_table->font();
        archivedFont.setItalic(true);
        const QBrush greyed(m_table->palette().color(QPalette::Disabled, QPalette::Text));

        for (int column = 0; column < m_table->columnCount(); ++column) {
            QTableWidgetItem* cell = m_table->item(row, column);
            if (cell == nullptr) {
                continue;
            }
            cell->setFont(archivedFont);
            cell->setForeground(greyed);
        }
    }

    // Every row says "Active" when archived rows are hidden, which is noise.
    // The column earns its place only while the toggle is on.
    m_table->setColumnHidden(static_cast<int>(Column::Status), !includeArchived);

    m_table->setSortingEnabled(true);
    updateActionButton();
}

void VesselListWidget::addVessel()
{
    VesselEditDialog dialog(m_repository, std::nullopt, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
        emit vesselsChanged();
    }
}

bool VesselListWidget::selectedVessel(QString* vesselId, QString* vesselName,
                                      bool* isArchived) const
{
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return false;
    }

    QTableWidgetItem* nameItem =
        m_table->item(selected.first()->row(), static_cast<int>(Column::Name));
    if (nameItem == nullptr) {
        return false;
    }

    *vesselId   = nameItem->data(kVesselIdRole).toString();
    *vesselName = nameItem->text();
    *isArchived = nameItem->data(kVesselArchivedRole).toBool();
    return !vesselId->isEmpty();
}

void VesselListWidget::updateActionButton()
{
    QString vesselId;
    QString vesselName;
    bool    isArchived = false;

    if (!selectedVessel(&vesselId, &vesselName, &isArchived)) {
        // Disabled, but still labelled: a button whose text changes to blank
        // when nothing is selected reads as broken rather than inactive.
        m_actionButton->setEnabled(false);
        m_actionButton->setText(tr("&Archive…"));
        return;
    }

    m_actionButton->setEnabled(true);
    // No ellipsis on Restore: the ellipsis promises a further question, and
    // Restore deliberately asks none (vessel-archive-spec §7).
    m_actionButton->setText(isArchived ? tr("&Restore") : tr("&Archive…"));
}

void VesselListWidget::archiveSelected()
{
    QString vesselId;
    QString vesselName;
    bool    isArchived = false;
    if (!selectedVessel(&vesselId, &vesselName, &isArchived) || isArchived) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Archive vessel"),
        tr("Archive '%1'?\n\n"
           "Its certificates will not be deleted — archiving only hides the vessel "
           "and its certificates from the fleet list and from alerts, and both can "
           "be brought back at any time with Restore.")
            .arg(vesselName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!m_repository.archive(vesselId)) {
        QMessageBox::warning(this, tr("Could not archive vessel"), m_repository.errorString());
        return;
    }

    reload();
    // Without this the toolbar's vessel selector would go on offering an
    // archived vessel and showing its certificates (certificate-crud-spec
    // §8.1, vessel-archive-spec §7).
    emit vesselsChanged();
}

void VesselListWidget::restoreSelected()
{
    QString vesselId;
    QString vesselName;
    bool    isArchived = false;
    if (!selectedVessel(&vesselId, &vesselName, &isArchived) || !isArchived) {
        return;
    }

    // No confirmation: restoring is the undo of archiving and costs nothing
    // if done by mistake, so a second question would be pure friction.
    if (!m_repository.restore(vesselId)) {
        QMessageBox::warning(this, tr("Could not restore vessel"), m_repository.errorString());
        return;
    }

    reload();
    emit vesselsChanged();
}

void VesselListWidget::editVessel(QTableWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }

    // The id lives on the first column, whichever cell was double-clicked.
    QTableWidgetItem* nameItem = m_table->item(item->row(), static_cast<int>(Column::Name));
    if (nameItem == nullptr) {
        return;
    }

    // Checked here, before findById() is ever called. findById() defaults to
    // excluding archived rows, so letting this fall through would return
    // nothing and trip the "no longer in the database" branch below — which
    // would be flatly untrue of a vessel sitting visible on screen
    // (vessel-archive-spec §7).
    if (nameItem->data(kVesselArchivedRole).toBool()) {
        QMessageBox::information(this, tr("Vessel is archived"),
                                 tr("This vessel is archived. Restore it to make changes."));
        return;
    }

    const QString id = nameItem->data(kVesselIdRole).toString();

    std::optional<Vessel> vessel;
    if (!m_repository.findById(id, &vessel)) {
        QMessageBox::warning(this, tr("Could not open vessel"), m_repository.errorString());
        return;
    }
    if (!vessel.has_value()) {
        // Another installation could have removed it between list and click.
        QMessageBox::information(this, tr("Vessel not found"),
                                 tr("This vessel is no longer in the database."));
        reload();
        return;
    }

    VesselEditDialog dialog(m_repository, vessel, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
        // An edit can rename a vessel, which changes how it reads in the
        // selector and where it sorts, so this fires for edits too.
        emit vesselsChanged();
    }
}
