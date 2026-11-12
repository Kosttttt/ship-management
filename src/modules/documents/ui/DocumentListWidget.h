#pragma once

#include "core/InstallationContext.h"

#include <QString>
#include <QWidget>

class DocumentActions;
class DocumentCategoryTabs;
class DocumentFileStore;
class DocumentRepository;
class VesselRepository;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// The Documents screen (document-management-spec.md §4.4).
//
// Shows fleet-wide documents plus the vessel the toolbar has selected — in
// VESSEL mode, plus its own, which the repository decides rather than this
// screen. A Scope column says which is which.
//
// Above the table: category tabs with counts, and a search box that filters
// by title or reference as it is typed.
class DocumentListWidget : public QWidget
{
    Q_OBJECT

public:
    DocumentListWidget(DocumentRepository&        repository,
                       DocumentFileStore&         files,
                       VesselRepository&          vessels,
                       const InstallationContext& installation,
                       QWidget*                   parent = nullptr);

    // Called through IModule::setSelectedVessel() when the toolbar's vessel
    // changes (§3.1). In VESSEL mode it is called once at start-up and the
    // repository ignores the value anyway.
    void setVesselId(const QString& vesselId);

    QString vesselId() const;

    void reload();

private:
    void addDocument();

    // The rows as the tabs and the search box leave them. Returns the counts
    // per category, which follow the search but not the selected tab — every
    // tab shows its own number whichever one is open, the same rule the
    // certificate list's tabs follow.
    void fillTable();

    DocumentRepository& m_repository;
    DocumentFileStore&  m_files;
    VesselRepository&   m_vessels;
    InstallationContext m_installation;

    QString m_vesselId;
    QString m_vesselName;

    QTableWidget*         m_table      = nullptr;
    DocumentCategoryTabs* m_tabs       = nullptr;
    DocumentActions*      m_actions    = nullptr;
    QLineEdit*            m_searchEdit = nullptr;
    QPushButton*          m_addButton  = nullptr;
};
