#include "modules/certificates/ui/EndorsementEditForm.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>

namespace {

QString labelFor(SurveyType type)
{
    switch (type) {
    case SurveyType::Annual:
        return EndorsementEditForm::tr("Annual survey");
    case SurveyType::Intermediate:
        return EndorsementEditForm::tr("Intermediate survey");
    case SurveyType::Initial:
        return EndorsementEditForm::tr("Initial survey");
    case SurveyType::Renewal:
        return EndorsementEditForm::tr("Renewal survey");
    case SurveyType::Unset:
        break;
    }
    return QString();
}

} // namespace

EndorsementEditForm::EndorsementEditForm(const QString&           certificateId,
                                         const QList<SurveyType>& allowedTypes,
                                         QWidget*                 parent)
    : QWidget(parent)
    , m_certificateId(certificateId)
{
    auto* layout = new QFormLayout(this);

    if (allowedTypes.size() == 1) {
        // One option is not a choice: the type is fixed and simply shown.
        m_onlyType  = allowedTypes.first();
        m_typeLabel = new QLabel(labelFor(m_onlyType), this);
        m_typeLabel->setObjectName(QStringLiteral("fixedSurveyTypeLabel"));
        layout->addRow(tr("Survey type:"), m_typeLabel);
    } else {
        m_typeCombo = new QComboBox(this);
        m_typeCombo->setObjectName(QStringLiteral("surveyTypeCombo"));
        // Only the types this certificate requires. INITIAL and RENEWAL are
        // never offered here (certificate-endorsement-spec §3, §6).
        for (SurveyType type : allowedTypes) {
            m_typeCombo->addItem(labelFor(type), static_cast<int>(type));
        }
        m_typeCombo->setCurrentIndex(-1);
        m_typeCombo->setPlaceholderText(tr("Choose a survey type…"));
        layout->addRow(tr("Survey &type:"), m_typeCombo);

        connect(m_typeCombo, &QComboBox::currentIndexChanged, this,
                [this]() { emit validityChanged(isValid()); });
    }

    m_dateEdit = new QDateEdit(this);
    m_dateEdit->setObjectName(QStringLiteral("endorsementDateEdit"));
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    m_dateEdit->setDate(QDate::currentDate());
    layout->addRow(tr("Survey &date:"), m_dateEdit);

    m_placeEdit    = new QLineEdit(this);
    m_surveyorEdit = new QLineEdit(this);
    m_surveyorEdit->setPlaceholderText(tr("Lloyd's Register"));
    m_resultEdit = new QLineEdit(this);
    m_resultEdit->setPlaceholderText(tr("Satisfactory"));

    layout->addRow(tr("&Place:"), m_placeEdit);
    layout->addRow(tr("&Surveyor:"), m_surveyorEdit);
    layout->addRow(tr("&Result:"), m_resultEdit);

    m_remarksEdit = new QPlainTextEdit(this);
    m_remarksEdit->setMaximumHeight(80);
    layout->addRow(tr("Re&marks:"), m_remarksEdit);
}

void EndorsementEditForm::setEndorsement(const Endorsement& endorsement)
{
    m_endorsementId = endorsement.id;
    if (!endorsement.certificateId.isEmpty()) {
        m_certificateId = endorsement.certificateId;
    }

    if (m_typeCombo == nullptr) {
        // Built with one allowed type, so there is nothing to choose. The
        // stored type can still differ from it — the requirement was unticked
        // after the survey was recorded — and when it does, the stored type
        // wins and the label is corrected to say so. Leaving the label reading
        // "Annual survey" over an intermediate row would be a quiet lie about
        // what happened.
        if (endorsement.surveyType != SurveyType::Unset) {
            m_onlyType = endorsement.surveyType;
            if (m_typeLabel != nullptr) {
                m_typeLabel->setText(labelFor(m_onlyType));
            }
        }
    } else {
        int index = m_typeCombo->findData(static_cast<int>(endorsement.surveyType));
        if (index < 0 && endorsement.surveyType != SurveyType::Unset) {
            // A type the certificate no longer requires. Offered rather than
            // dropped: editing the place a survey happened must not quietly
            // change which survey it was.
            m_typeCombo->addItem(labelFor(endorsement.surveyType),
                                 static_cast<int>(endorsement.surveyType));
            index = m_typeCombo->count() - 1;
        }
        m_typeCombo->setCurrentIndex(index);
    }

    if (endorsement.endorsementDate.isValid()) {
        m_dateEdit->setDate(endorsement.endorsementDate);
    }
    m_placeEdit->setText(endorsement.place);
    m_surveyorEdit->setText(endorsement.surveyor);
    m_resultEdit->setText(endorsement.result);
    m_remarksEdit->setPlainText(endorsement.remarks);

    emit validityChanged(isValid());
}

Endorsement EndorsementEditForm::endorsement() const
{
    Endorsement endorsement;
    endorsement.id            = m_endorsementId;
    endorsement.certificateId = m_certificateId;

    if (m_typeCombo == nullptr) {
        endorsement.surveyType = m_onlyType;
    } else if (m_typeCombo->currentIndex() >= 0) {
        endorsement.surveyType = static_cast<SurveyType>(m_typeCombo->currentData().toInt());
    }

    endorsement.endorsementDate = m_dateEdit->date();
    endorsement.place           = m_placeEdit->text().trimmed();
    endorsement.surveyor        = m_surveyorEdit->text().trimmed();
    endorsement.result          = m_resultEdit->text().trimmed();
    endorsement.remarks         = m_remarksEdit->toPlainText().trimmed();
    return endorsement;
}

bool EndorsementEditForm::isValid() const
{
    // The date edit always holds a date; the type is the only thing that can
    // be missing. The repository re-checks both, plus "not before issue".
    return endorsement().surveyType != SurveyType::Unset;
}

void EndorsementEditForm::setFocusOnFirstField()
{
    if (m_typeCombo != nullptr) {
        m_typeCombo->setFocus();
    } else {
        m_dateEdit->setFocus();
    }
}
