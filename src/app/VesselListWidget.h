#pragma once

#include <QString>
#include <QWidget>

class VesselArchiveActions;
class VesselRepository;
class QCheckBox;
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

    VesselRepository& m_repository;
    QTableWidget*     m_table        = nullptr;
    QCheckBox*        m_showArchived = nullptr;

    // Owns the Archive / Restore button and both actions (vessel-archive-spec
    // §10.2). This screen keeps the table, the reload and the edit dialog.
    VesselArchiveActions* m_archiveActions = nullptr;
};
