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
    // filename is built from the row's own number, name and expiry date, and on
    // Add there is no row and no id until create() has returned one (§3).
    //
    // expiryDateForName is the certificate's new expiry date on a renewal and
    // its current one otherwise — an ordinary edit is not a renewal and has no
    // new date to name a document after.
    //
    // A failure here warns but still accepts, the same call step 10 made for a
    // failed survey clear: the certificate itself is already saved.
    void replaceAttachmentIfChosen(const Certificate& saved);

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
