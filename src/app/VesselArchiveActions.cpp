#include "app/VesselArchiveActions.h"

#include "core/VesselRepository.h"

#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

VesselArchiveActions::VesselArchiveActions(VesselRepository& repository, QTableWidget* table,
                                           QWidget* dialogParent)
    : QObject(dialogParent)
    , m_repository(repository)
    , m_table(table)
    , m_dialogParent(dialogParent)
{
    m_button = new QPushButton(tr("&Archive…"), dialogParent);
    m_button->setObjectName(QStringLiteral("archiveVesselButton"));
    m_button->setEnabled(false);

    // Which of the two it is gets decided when it is pressed, from the row
    // selected at that moment, so the button itself holds no state.
    connect(m_button, &QPushButton::clicked, this, [this] {
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

    if (m_table != nullptr) {
        connect(m_table, &QTableWidget::itemSelectionChanged,
                this, &VesselArchiveActions::updateState);
    }
}

QPushButton* VesselArchiveActions::button() const
{
    return m_button;
}

bool VesselArchiveActions::selectedVessel(QString* vesselId, QString* vesselName,
                                          bool* isArchived) const
{
    if (m_table == nullptr) {
        return false;
    }

    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return false;
    }

    QTableWidgetItem* nameItem =
        m_table->item(selected.first()->row(), VesselTableRoles::kNameColumn);
    if (nameItem == nullptr) {
        return false;
    }

    *vesselId   = nameItem->data(VesselTableRoles::kVesselId).toString();
    *vesselName = nameItem->text();
    *isArchived = nameItem->data(VesselTableRoles::kVesselArchived).toBool();
    return !vesselId->isEmpty();
}

void VesselArchiveActions::updateState()
{
    QString vesselId;
    QString vesselName;
    bool    isArchived = false;

    if (!selectedVessel(&vesselId, &vesselName, &isArchived)) {
        // Disabled, but still labelled: a button whose text goes blank when
        // nothing is selected reads as broken rather than inactive.
        m_button->setEnabled(false);
        m_button->setText(tr("&Archive…"));
        return;
    }

    m_button->setEnabled(true);
    // No ellipsis on Restore: the ellipsis promises a further question, and
    // Restore deliberately asks none (vessel-archive-spec §7).
    m_button->setText(isArchived ? tr("&Restore") : tr("&Archive…"));
}

void VesselArchiveActions::archiveSelected()
{
    QString vesselId;
    QString vesselName;
    bool    isArchived = false;
    if (!selectedVessel(&vesselId, &vesselName, &isArchived) || isArchived) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        m_dialogParent, tr("Archive vessel"),
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
        QMessageBox::warning(m_dialogParent, tr("Could not archive vessel"),
                             m_repository.errorString());
        return;
    }

    emit fleetChanged();
}

void VesselArchiveActions::restoreSelected()
{
    QString vesselId;
    QString vesselName;
    bool    isArchived = false;
    if (!selectedVessel(&vesselId, &vesselName, &isArchived) || !isArchived) {
        return;
    }

    // No confirmation: restoring is the undo of archiving and costs nothing
    // if done by mistake, so a second question would be pure friction
    // (vessel-archive-spec §7).
    if (!m_repository.restore(vesselId)) {
        QMessageBox::warning(m_dialogParent, tr("Could not restore vessel"),
                             m_repository.errorString());
        return;
    }

    emit fleetChanged();
}
