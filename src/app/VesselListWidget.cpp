#include "app/VesselListWidget.h"

#include "app/VesselEditDialog.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// The vessel id is carried on the row's first cell so an edit knows which row
// it is editing without showing a UUID to the user.
constexpr int kVesselIdRole = Qt::UserRole + 1;

enum class Column {
    Name      = 0,
    ImoNumber = 1,
    FlagState = 2,
    CallSign  = 3
};

} // namespace

VesselListWidget::VesselListWidget(VesselRepository& repository, QWidget* parent)
    : QWidget(parent)
    , m_repository(repository)
{
    auto* addButton = new QPushButton(tr("&Add Vessel"), this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("IMO Number"), tr("Flag State"), tr("Call Sign")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // edited via the dialog
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->sortByColumn(static_cast<int>(Column::Name), Qt::AscendingOrder);

    auto* hint = new QLabel(tr("Double-click a vessel to edit it."), this);
    hint->setEnabled(false); // renders as secondary text

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(addButton, 0, Qt::AlignLeft);
    layout->addWidget(m_table);
    layout->addWidget(hint);

    connect(addButton, &QPushButton::clicked, this, &VesselListWidget::addVessel);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &VesselListWidget::editVessel);

    reload();
}

void VesselListWidget::reload()
{
    QList<Vessel> vessels;
    if (!m_repository.list(&vessels)) {
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

        m_table->setItem(row, static_cast<int>(Column::Name), nameItem);
        m_table->setItem(row, static_cast<int>(Column::ImoNumber),
                         new QTableWidgetItem(vessel.imoNumber));
        m_table->setItem(row, static_cast<int>(Column::FlagState),
                         new QTableWidgetItem(vessel.flagState));
        m_table->setItem(row, static_cast<int>(Column::CallSign),
                         new QTableWidgetItem(vessel.callSign));
    }

    m_table->setSortingEnabled(true);
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
