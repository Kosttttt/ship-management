#pragma once

#include <QString>
#include <QWidget>

class VesselRepository;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// OFFICE only: the fleet list (vessel-crud-spec §4).
//
// A QTableWidget rather than a model/view pair. With four text columns and a
// small fleet it is far less machinery for the same result; the certificate
// list in step 8 has real sorting and filtering needs and will earn a proper
// QAbstractTableModel then (CLAUDE.md §10 — no abstraction without a reason).
class VesselListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VesselListWidget(VesselRepository& repository, QWidget* parent = nullptr);

    // Re-reads from the repository. Public so the window can refresh it.
    void reload();

signals:
    // Emitted after a successful add, edit, archive or restore. Nothing
    // outside this screen needed to know when the fleet changed until the
    // certificates toolbar selector arrived, which is why the defect it fixes
    // went unnoticed (certificate-crud-spec §8.1).
    //
    // Archive and restore emit it for exactly that reason: without it the
    // repository would be correct while the toolbar kept offering an archived
    // vessel and showing its certificates (vessel-archive-spec §7).
    void vesselsChanged();

private:
    void addVessel();
    void editVessel(QTableWidgetItem* item);
    void archiveSelected();
    void restoreSelected();

    // Archive or Restore depending on what is selected, disabled when nothing
    // is — one button rather than two, the same enabled-state pattern
    // CertificateDeleteActions uses (vessel-archive-spec §7).
    void updateActionButton();

    // False when no row is selected. Reads the id and archived flag straight
    // off the row's own item data, so neither needs a second query.
    bool selectedVessel(QString* vesselId, QString* vesselName, bool* isArchived) const;

    VesselRepository& m_repository;
    QTableWidget*     m_table        = nullptr;
    QCheckBox*        m_showArchived = nullptr;
    QPushButton*      m_actionButton = nullptr;
};
