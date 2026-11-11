#pragma once

#include <QObject>
#include <QString>

class VesselRepository;
class QPushButton;
class QTableWidget;
class QWidget;

// Where a vessel row carries what the buttons need to know. Shared rather
// than duplicated, because VesselListWidget writes these and this class
// reads them, and a disagreement between the two would be silent.
namespace VesselTableRoles {

// Both ride on the row's first cell, so neither the button nor the
// double-click handler has to ask the database what is selected.
constexpr int kVesselId       = Qt::UserRole + 1;
constexpr int kVesselArchived = Qt::UserRole + 2;

// The column those roles are attached to. VesselListWidget's own Column
// enum is defined from this, so the two cannot drift apart.
constexpr int kNameColumn = 0;

} // namespace VesselTableRoles

// The vessel list's Archive / Restore button (vessel-archive-spec §7, split
// out of VesselListWidget by §10.2).
//
// The same seam CertificateDeleteActions was split along, for the same
// reason: the widget had reached 319 lines, over the ~300 guideline in
// CLAUDE.md §9, and archiving was what pushed it there. The split is a real
// one and not a line count being satisfied — everything here is about one
// pair of mutually exclusive actions on a single selected row, while what
// stays behind in the widget is the table, the fleet reload and the edit
// dialog.
//
// It owns its button, so the rule deciding the label and the enabled state
// lives next to the code that acts on it. The widget still places that
// button in its own layout, and the button keeps the position it had, so
// nothing that looks for it had to change.
class VesselArchiveActions : public QObject
{
    Q_OBJECT

public:
    // Parented to dialogParent, which is both the QObject owner and the
    // window every message box raised from here is centred on.
    VesselArchiveActions(VesselRepository& repository, QTableWidget* table,
                         QWidget* dialogParent);

    QPushButton* button() const;

    // One button, not two: Archive for an active vessel, Restore for an
    // archived one, disabled when nothing is selected. The table is
    // SingleSelection, so there is no "more than one" case to rule out the
    // way CertificateDeleteActions has to.
    void updateState();

signals:
    // A vessel was archived or restored, so both the fleet list and anything
    // showing the fleet elsewhere are stale. The widget turns this into its
    // own reload plus vesselsChanged() — the signal the certificates toolbar
    // selector depends on (certificate-crud-spec §8.1).
    void fleetChanged();

private:
    void archiveSelected();
    void restoreSelected();

    // False when nothing is selected. Reads both values straight off the
    // row's own item data, so neither needs a second query.
    bool selectedVessel(QString* vesselId, QString* vesselName, bool* isArchived) const;

    VesselRepository& m_repository;
    QTableWidget*     m_table        = nullptr;
    QWidget*          m_dialogParent = nullptr;
    QPushButton*      m_button       = nullptr;
};
