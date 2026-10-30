#pragma once

#include "modules/certificates/domain/Certificate.h"

#include <QDialog>
#include <QString>

class AttachmentSourceField;
class CertificateAttachmentService;
class CertificateExtensionRepository;
class QDateEdit;
class QLineEdit;
class QPushButton;

// Starts one short-term extension against an existing certificate
// (certificate-extension-spec §7), optionally attaching the short-term
// certificate's own document (certificate-attachment-spec §8).
//
// Shaped like EndorsementEditDialog, but with its fields built in directly
// rather than in a separate *EditForm widget: there are only three of them, and
// nothing else in the app needs to embed them anywhere. Start-only — resolving
// an extension needs no dialog at all, since nothing is being destroyed that
// could not simply be started again.
class CertificateExtensionDialog : public QDialog
{
    Q_OBJECT

public:
    // Takes the whole certificate rather than its id and name: the document's
    // stored filename is built from its number, its name and the extension's
    // own expiry date (certificate-attachment-spec §6).
    CertificateExtensionDialog(CertificateExtensionRepository& repository,
                               CertificateAttachmentService&   attachments,
                               const Certificate&              certificate,
                               QWidget*                        parent = nullptr);

protected:
    void accept() override;

private:
    CertificateExtensionRepository& m_repository;
    CertificateAttachmentService&   m_attachments;
    Certificate                     m_certificate;

    QDateEdit*             m_expiryEdit       = nullptr;
    QLineEdit*             m_noteEdit         = nullptr;
    AttachmentSourceField* m_attachmentSource = nullptr;
    QPushButton*           m_okButton         = nullptr;
};
