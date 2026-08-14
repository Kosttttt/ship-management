#pragma once

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/Endorsement.h"

#include <QDialog>
#include <QList>

#include <optional>

class CertificateEditForm;
class CertificateRepository;
class EndorsementRepository;
class QGroupBox;
class QPushButton;
class QTableWidget;

// Wraps CertificateEditForm in an OK/Cancel dialog, used for both Add and
// Edit — the same shape as step 4's VesselEditDialog.
class CertificateEditDialog : public QDialog
{
    Q_OBJECT

public:
    CertificateEditDialog(CertificateRepository&            repository,
                          EndorsementRepository&            endorsements,
                          const QString&                    vesselId,
                          const std::optional<Certificate>& existing,
                          QWidget*                          parent = nullptr);

protected:
    // The write happens here so a failure can keep the dialog open with the
    // typed data intact.
    void accept() override;

private:
    // certificate-endorsement-spec §6: shown only when editing a saved
    // certificate that requires at least one kind of survey. A brand-new
    // certificate has no id for an endorsement to point at, and one that
    // needs no survey has nothing meaningful to record.
    QGroupBox* buildEndorsementsSection(const Certificate& certificate);
    void       reloadEndorsements();
    void       addEndorsement();

    // The survey types this certificate actually requires.
    QList<SurveyType> allowedSurveyTypes() const;

    CertificateRepository& m_repository;
    EndorsementRepository& m_endorsements;

    CertificateEditForm* m_form      = nullptr;
    QPushButton*         m_okButton  = nullptr;
    bool                 m_isEditing = false;

    QString    m_certificateId;
    bool       m_requiresAnnual       = false;
    bool       m_requiresIntermediate = false;

    QTableWidget* m_endorsementTable = nullptr;
};
