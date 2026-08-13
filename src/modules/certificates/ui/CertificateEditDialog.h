#pragma once

#include "modules/certificates/domain/Certificate.h"

#include <QDialog>

#include <optional>

class CertificateEditForm;
class CertificateRepository;
class QPushButton;

// Wraps CertificateEditForm in an OK/Cancel dialog, used for both Add and
// Edit — the same shape as step 4's VesselEditDialog.
class CertificateEditDialog : public QDialog
{
    Q_OBJECT

public:
    CertificateEditDialog(CertificateRepository&            repository,
                          const QString&                    vesselId,
                          const std::optional<Certificate>& existing,
                          QWidget*                          parent = nullptr);

protected:
    // The write happens here so a failure can keep the dialog open with the
    // typed data intact.
    void accept() override;

private:
    CertificateRepository& m_repository;
    CertificateEditForm*   m_form      = nullptr;
    QPushButton*           m_okButton  = nullptr;
    bool                   m_isEditing = false;
};
