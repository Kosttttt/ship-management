#pragma once

#include "modules/certificates/domain/Endorsement.h"

#include <QList>
#include <QWidget>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// The fields of one endorsement (certificate-endorsement-spec §6), in the same
// form-wrapped-in-a-dialog shape every other add/edit screen uses.
class EndorsementEditForm : public QWidget
{
    Q_OBJECT

public:
    // allowedTypes are the survey types this particular certificate actually
    // requires. When there is only one, no combo is built at all — asking
    // someone to choose between a single option is friction, not a decision
    // (certificate-endorsement-spec §6).
    EndorsementEditForm(const QString&           certificateId,
                        const QList<SurveyType>& allowedTypes,
                        QWidget*                 parent = nullptr);

    Endorsement endorsement() const;

    // Fills the fields from an endorsement already recorded, for the edit mode
    // added by certificate-attachment-addendum-2-spec §6. The id and the
    // certificate id ride along so that endorsement() hands back the same row
    // rather than a new one.
    //
    // A stored survey type the certificate no longer requires — the
    // requirement was unticked after the survey was recorded — is added to the
    // combo rather than silently dropped, because an edit must not be able to
    // change what kind of survey happened by accident.
    void setEndorsement(const Endorsement& endorsement);

    bool isValid() const;
    void setFocusOnFirstField();

signals:
    void validityChanged(bool valid);

private:
    QString    m_certificateId;
    QString    m_endorsementId; // empty while adding
    SurveyType m_onlyType = SurveyType::Unset;

    QComboBox*      m_typeCombo = nullptr;   // null when only one type is allowed
    QLabel*         m_typeLabel = nullptr;   // the other way round: null when there is a combo
    QDateEdit*      m_dateEdit  = nullptr;
    QLineEdit*      m_placeEdit = nullptr;
    QLineEdit*      m_surveyorEdit = nullptr;
    QLineEdit*      m_resultEdit   = nullptr;
    QPlainTextEdit* m_remarksEdit  = nullptr;
};
