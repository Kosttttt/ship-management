#pragma once

#include <QWidget>

class InstallationContext;
class VesselEditForm;
class VesselRepository;
class QLabel;
class QPushButton;

// VESSEL only: the one ship this installation owns (vessel-crud-spec §4).
//
// The form is embedded directly, with no dialog chrome, because there is no
// list to return to — this screen is always on screen. "Discard changes"
// therefore reloads from the database rather than closing anything.
class VesselDetailPage : public QWidget
{
    Q_OBJECT

public:
    VesselDetailPage(VesselRepository&          repository,
                     const InstallationContext& installation,
                     QWidget*                   parent = nullptr);

    // Re-reads this installation's vessel from the database.
    void reload();

private:
    void save();

    VesselRepository&          m_repository;
    const InstallationContext& m_installation;
    VesselEditForm*            m_form        = nullptr;
    QPushButton*               m_saveButton  = nullptr;
    QLabel*                    m_statusLabel = nullptr;
};
