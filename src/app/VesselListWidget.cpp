#include "app/VesselListWidget.h"

#include "app/VesselArchiveActions.h"
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

// The vessel id and its archived flag are carried on the row's first cell so
// an edit knows which row it is editing without showing a UUID to the user,
// and so the Archive button knows which way round it is. Declared in
// VesselArchiveActions.h because both files depend on them agreeing.
constexpr int kVesselIdRole       = VesselTableRoles::kVesselId;
constexpr int kVesselArchivedRole = VesselTableRoles::kVesselArchived;

enum class Column {
    // Defined from the shared constant rather than repeating 0, so the column
    // the roles are attached to cannot drift away from the column this screen
    // puts them on.
    Name      = VesselTableRoles::kNameColumn,
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

    // Built after the table, which it listens to for the selection.
    m_archiveActions = new VesselArchiveActions(m_repository, m_table, this);

    auto* controls = new QHBoxLayout;
    controls->addWidget(addButton);
    controls->addWidget(m_archiveActions->button());
    controls->addStretch();
    controls->addWidget(m_showArchived);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(m_table);
    layout->addWidget(hint);

    connect(addButton, &QPushButton::clicked, this, &VesselListWidget::addVessel);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &VesselListWidget::editVessel);
    connect(m_showArchived, &QCheckBox::toggled, this, &VesselListWidget::reload);

    // Archiving or restoring changes the fleet, so the list is reloaded and
    // the same signal an add or an edit raises goes out. Without the second
    // half the toolbar's vessel selector would go on offering an archived
    // vessel and showing its certificates (certificate-crud-spec §8.1,
    // vessel-archive-spec §7) — which is exactly what moving this code out of
    // here could have quietly lost, so it has its own test.
    connect(m_archiveActions, &VesselArchiveActions::fleetChanged, this, [this] {
        reload();
        emit vesselsChanged();
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

    // Rebuilding the rows clears the selection, so the button has to be told
    // rather than waiting for a selection change that is not coming.
    m_archiveActions->updateState();
}

void VesselListWidget::addVessel()
{
    VesselEditDialog dialog(m_repository, std::nullopt, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
        emit vesselsChanged();
    }
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
