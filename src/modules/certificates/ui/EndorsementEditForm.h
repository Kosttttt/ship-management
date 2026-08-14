#pragma once

#include "modules/certificates/domain/Endorsement.h"

#include <QList>
#include <QWidget>

class QComboBox;
class QDateEdit;
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

    bool isValid() const;
    void setFocusOnFirstField();

signals:
    void validityChanged(bool valid);

private:
    QString    m_certificateId;
    SurveyType m_onlyType = SurveyType::Unset;

    QComboBox*      m_typeCombo = nullptr;   // null when only one type is allowed
    QDateEdit*      m_dateEdit  = nullptr;
    QLineEdit*      m_placeEdit = nullptr;
    QLineEdit*      m_surveyorEdit = nullptr;
    QLineEdit*      m_resultEdit   = nullptr;
    QPlainTextEdit* m_remarksEdit  = nullptr;
};
