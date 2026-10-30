#pragma once

#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/domain/Endorsement.h"

#include <QDialog>
#include <QList>

#include <optional>

class AttachmentSourceField;
class CertificateAttachmentService;
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
    // isRenewal defaults to false, so every existing call site keeps today's
    // behaviour. In renewal mode the title says so, the Endorsements section
    // is not shown, and a successful Save also clears the certificate's
    // recorded surveys (certificate-renewal-spec §6).
    CertificateEditDialog(CertificateRepository&            repository,
                          EndorsementRepository&            endorsements,
                          CertificateAttachmentService&     attachments,
                          const QString&                    vesselId,
                          const std::optional<Certificate>& existing,
                          bool                              isRenewal = false,
                          QWidget*                          parent    = nullptr);

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

    // certificate-attachment-spec §8: shown only in renewal mode. A renewal is
    // the moment the paper document is actually replaced, so the offer belongs
    // next to it; an ordinary edit changes no document.
    QGroupBox* buildAttachmentSection();

    // Run after the certificate update has succeeded, using the certificate's
    // *new* expiry date for the stored filename. A failure here warns but
    // still accepts, the same call step 10 made for a failed survey clear.
    void replaceAttachmentIfChosen(const Certificate& renewed);

    // The survey types this certificate actually requires.
    QList<SurveyType> allowedSurveyTypes() const;

    CertificateRepository&        m_repository;
    EndorsementRepository&        m_endorsements;
    CertificateAttachmentService& m_attachments;

    CertificateEditForm* m_form      = nullptr;
    QPushButton*         m_okButton  = nullptr;
    bool                 m_isEditing = false;
    bool                 m_isRenewal = false;

    QString    m_certificateId;
    bool       m_requiresAnnual       = false;
    bool       m_requiresIntermediate = false;

    QTableWidget*          m_endorsementTable = nullptr;
    AttachmentSourceField* m_attachmentSource = nullptr;
};
