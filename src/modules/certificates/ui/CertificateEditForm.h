#pragma once

#include "modules/certificates/domain/Certificate.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QPlainTextEdit;

// The thirteen editable certificate fields, grouped into sections inside a
// scroll area so the form stays usable on a small screen
// (certificate-crud-spec §5).
//
// A plain QWidget rather than a dialog, so the same form can be reused the way
// VesselEditForm is — wrapped in CertificateEditDialog today, embedded
// somewhere else if a later step wants that.
class CertificateEditForm : public QWidget
{
    Q_OBJECT

public:
    explicit CertificateEditForm(QWidget* parent = nullptr);

    // Fills the fields. The id and vessel id are remembered and handed back by
    // certificate(); neither is ever shown.
    void setCertificate(const Certificate& certificate);
    Certificate certificate() const;

    // For a brand-new certificate: says which vessel it will belong to without
    // touching any of the fields, so the form keeps its own sensible defaults.
    // Passing a default-constructed Certificate to setCertificate() instead
    // would tick "does not expire", because a blank expiry date means exactly
    // that — a correct reading of the data, and quite the wrong default for a
    // form the user is about to fill in.
    void setVesselId(const QString& vesselId);

    bool isValid() const;
    void setFocusOnFirstField();

signals:
    void validityChanged(bool valid);

private:
    void buildLayout();
    // Applies the no-expiry rule: a certificate that never expires has no
    // anniversary to schedule a survey against, so the survey controls are
    // cleared and switched off (certificate-crud-spec §4).
    void applyExpiryRule();
    void announceValidity();

    QLineEdit*      m_listNumberEdit    = nullptr;
    QLineEdit*      m_nameEdit          = nullptr;
    QComboBox*      m_categoryCombo     = nullptr;
    QLineEdit*      m_numberEdit        = nullptr;
    QLineEdit*      m_appliesToEdit     = nullptr;

    QDateEdit*      m_issueDateEdit     = nullptr;
    QDateEdit*      m_expiryDateEdit    = nullptr;
    QCheckBox*      m_noExpiryCheck     = nullptr;
    QCheckBox*      m_interimCheck      = nullptr;

    QLineEdit*      m_issuedByEdit      = nullptr;
    QLineEdit*      m_placeOfIssueEdit  = nullptr;

    QCheckBox*      m_annualCheck       = nullptr;
    QCheckBox*      m_intermediateCheck = nullptr;
    QComboBox*      m_intermediateMode  = nullptr;

    QPlainTextEdit* m_notesEdit         = nullptr;

    QString m_id;
    QString m_vesselId;
};
