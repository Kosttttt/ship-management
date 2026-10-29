#pragma once

#include <QDialog>
#include <QString>

class CertificateExtensionRepository;
class QDateEdit;
class QLineEdit;
class QPushButton;

// Starts one short-term extension against an existing certificate
// (certificate-extension-spec §7).
//
// Shaped like EndorsementEditDialog, but with its fields built in directly
// rather than in a separate *EditForm widget: there are only two of them, and
// nothing else in the app needs to embed them anywhere. Start-only — resolving
// an extension needs no dialog at all, since nothing is being destroyed that
// could not simply be started again.
class CertificateExtensionDialog : public QDialog
{
    Q_OBJECT

public:
    CertificateExtensionDialog(CertificateExtensionRepository& repository,
                               const QString&                  certificateId,
                               const QString&                  certificateName,
                               QWidget*                        parent = nullptr);

protected:
    void accept() override;

private:
    CertificateExtensionRepository& m_repository;
    QString                         m_certificateId;

    QDateEdit*   m_expiryEdit = nullptr;
    QLineEdit*   m_noteEdit   = nullptr;
    QPushButton* m_okButton   = nullptr;
};
