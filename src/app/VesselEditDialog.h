#pragma once

#include "core/Vessel.h"

#include <QDialog>

#include <optional>

class VesselEditForm;
class VesselRepository;
class QPushButton;

// OFFICE only: wraps VesselEditForm in an OK/Cancel dialog, used for both Add
// and Edit (vessel-crud-spec §4).
//
// Which of the two it is depends on whether an existing vessel was passed in,
// so there is one dialog rather than two nearly identical ones.
class VesselEditDialog : public QDialog
{
    Q_OBJECT

public:
    VesselEditDialog(VesselRepository&             repository,
                     const std::optional<Vessel>&  existing,
                     QWidget*                      parent = nullptr);

protected:
    // The write happens here so a failure can keep the dialog open with the
    // typed data intact, the same pattern FirstRunWizard uses.
    void accept() override;

private:
    VesselRepository& m_repository;
    VesselEditForm*   m_form       = nullptr;
    QPushButton*      m_okButton   = nullptr;
    bool              m_isEditing  = false;
};
