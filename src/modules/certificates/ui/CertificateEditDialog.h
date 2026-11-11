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
    // certificate-attachment-spec §8, widened by
    // certificate-attachment-addendum-spec §3: shown in every mode now, not
    // only renewal. A certificate's real document is as often already in hand
    // the first time it is entered as it is at renewal time, and an ordinary
    // edit is where somebody who has just scanned it goes looking.
    //
    // Only the empty-state wording differs between the three modes: Add has
    // nothing to keep, while Edit and Renewal may both already have a document
    // that must obviously survive doing nothing.
    QGroupBox* buildAttachmentSection();

    // Run after the certificate write has succeeded, never before: the stored
    // filename is built from the row's own number, name and dates, and on Add
    // there is no row and no id until create() has returned one (§3).
    //
    // Which date the name carries is the service's decision since
    // certificate-attachment-addendum-2-spec §7 — whichever of expiry, the
    // next annual window and the next intermediate window is soonest — so
    // what this hands over is simply the certificate as it now stands.
    //
    // A failure here warns but still accepts, the same call step 10 made for a
    // failed survey clear: the certificate itself is already saved.
    void replaceAttachmentIfChosen(const Certificate& saved);

    // The survey types this certificate actually requires.
    QList<SurveyType> allowedSurveyTypes() const;

    CertificateRepository&        m_repository;
    EndorsementRepository&        m_endorsements;
    CertificateAttachmentService& m_attachments;

    // Kept because the attachment control needs the vessel's folder, which is
    // looked up when that section is built (housekeeping-round-spec §5).
    QString                       m_vesselId;

    CertificateEditForm* m_form      = nullptr;
    QPushButton*         m_okButton  = nullptr;
    bool                 m_isEditing = false;
    bool                 m_isRenewal = false;

    QString m_certificateId;
    bool    m_requiresAnnual       = false;
    bool    m_requiresIntermediate = false;

    AttachmentSourceField* m_attachmentSource = nullptr;
};
