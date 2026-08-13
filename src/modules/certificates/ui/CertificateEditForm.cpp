#include "modules/certificates/ui/CertificateEditForm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// The category enum travels in the combo's item data, so the order of the
// items is never load-bearing.
void fillCategories(QComboBox* combo)
{
    combo->addItem(CertificateEditForm::tr("Statutory"),
                   static_cast<int>(CertificateCategory::Statutory));
    combo->addItem(CertificateEditForm::tr("Class"),
                   static_cast<int>(CertificateCategory::Class));
    combo->addItem(CertificateEditForm::tr("Equipment"),
                   static_cast<int>(CertificateCategory::Equipment));
    combo->addItem(CertificateEditForm::tr("Other"),
                   static_cast<int>(CertificateCategory::Other));
}

void fillIntermediateModes(QComboBox* combo)
{
    combo->addItem(CertificateEditForm::tr("Additional to the annual surveys"),
                   static_cast<int>(IntermediateMode::Additional));
    combo->addItem(CertificateEditForm::tr("Replaces one annual survey"),
                   static_cast<int>(IntermediateMode::ReplacesAnnual));
}

QDateEdit* makeDateEdit(QWidget* parent)
{
    auto* edit = new QDateEdit(parent);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    edit->setDate(QDate::currentDate());
    return edit;
}

} // namespace

CertificateEditForm::CertificateEditForm(QWidget* parent)
    : QWidget(parent)
{
    buildLayout();

    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { announceValidity(); });
    connect(m_categoryCombo, &QComboBox::currentIndexChanged, this,
            [this]() { announceValidity(); });

    connect(m_noExpiryCheck, &QCheckBox::toggled, this, [this]() {
        applyExpiryRule();
        announceValidity();
    });
    connect(m_intermediateCheck, &QCheckBox::toggled, this, [this]() { applyExpiryRule(); });

    applyExpiryRule();
}

void CertificateEditForm::buildLayout()
{
    m_listNumberEdit = new QLineEdit;
    m_listNumberEdit->setObjectName(QStringLiteral("listNumberEdit"));
    m_listNumberEdit->setPlaceholderText(tr("15D"));
    m_listNumberEdit->setMaximumWidth(120);
    // The validator refuses any keystroke that would leave the field outside
    // "digits, then letters", so an invalid character never appears on screen
    // — the same "the widget enforces the rule" approach as the gross tonnage
    // spin box in step 4 (certificate-crud-spec §8.2).
    m_listNumberEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(CertificateListNumber::pattern()),
                                        m_listNumberEdit));

    m_nameEdit = new QLineEdit;
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    m_nameEdit->setPlaceholderText(tr("Cargo Ship Safety Construction Certificate"));

    m_categoryCombo = new QComboBox;
    m_categoryCombo->setObjectName(QStringLiteral("categoryCombo"));
    fillCategories(m_categoryCombo);
    // Nothing preselected: the category is a decision, not a default.
    m_categoryCombo->setCurrentIndex(-1);
    m_categoryCombo->setPlaceholderText(tr("Choose a category…"));

    m_numberEdit    = new QLineEdit;
    m_appliesToEdit = new QLineEdit;
    m_appliesToEdit->setPlaceholderText(tr("Liferaft No. 3, S/N 44821"));

    auto* identity       = new QGroupBox(tr("Identity"));
    auto* identityLayout = new QFormLayout(identity);
    identityLayout->addRow(tr("N&o.:"), m_listNumberEdit);
    identityLayout->addRow(tr("&Name:"), m_nameEdit);
    identityLayout->addRow(tr("&Category:"), m_categoryCombo);
    identityLayout->addRow(tr("Certificate n&umber:"), m_numberEdit);
    identityLayout->addRow(tr("&Applies to:"), m_appliesToEdit);

    m_issueDateEdit  = makeDateEdit(this);
    m_expiryDateEdit = makeDateEdit(this);
    m_expiryDateEdit->setDate(QDate::currentDate().addYears(5)); // the usual term
    m_noExpiryCheck = new QCheckBox(tr("This certificate does not e&xpire"));
    m_noExpiryCheck->setObjectName(QStringLiteral("noExpiryCheck"));
    m_interimCheck = new QCheckBox(tr("&Interim certificate"));
    m_interimCheck->setObjectName(QStringLiteral("interimCheck"));

    auto* validity       = new QGroupBox(tr("Validity"));
    auto* validityLayout = new QFormLayout(validity);
    validityLayout->addRow(tr("Issue &date:"), m_issueDateEdit);
    validityLayout->addRow(tr("&Expiry date:"), m_expiryDateEdit);
    validityLayout->addRow(QString(), m_noExpiryCheck);
    validityLayout->addRow(QString(), m_interimCheck);

    m_issuedByEdit     = new QLineEdit;
    m_placeOfIssueEdit = new QLineEdit;

    auto* authority       = new QGroupBox(tr("Issuing authority"));
    auto* authorityLayout = new QFormLayout(authority);
    authorityLayout->addRow(tr("Issued &by:"), m_issuedByEdit);
    authorityLayout->addRow(tr("&Place of issue:"), m_placeOfIssueEdit);

    m_annualCheck = new QCheckBox(tr("Requires an ann&ual survey"));
    m_annualCheck->setObjectName(QStringLiteral("annualSurveyCheck"));
    m_intermediateCheck = new QCheckBox(tr("Requires an inter&mediate survey"));
    m_intermediateCheck->setObjectName(QStringLiteral("intermediateSurveyCheck"));
    m_intermediateMode = new QComboBox;
    m_intermediateMode->setObjectName(QStringLiteral("intermediateModeCombo"));
    fillIntermediateModes(m_intermediateMode);

    auto* surveys       = new QGroupBox(tr("Survey requirements"));
    auto* surveysLayout = new QFormLayout(surveys);
    surveysLayout->addRow(QString(), m_annualCheck);
    surveysLayout->addRow(QString(), m_intermediateCheck);
    surveysLayout->addRow(tr("Intermediate su&rvey:"), m_intermediateMode);

    m_notesEdit = new QPlainTextEdit;
    m_notesEdit->setMaximumHeight(90);

    auto* notes       = new QGroupBox(tr("Notes"));
    auto* notesLayout = new QVBoxLayout(notes);
    notesLayout->addWidget(m_notesEdit);

    auto* inner       = new QWidget;
    auto* innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(0, 0, 0, 0);
    for (QGroupBox* group : {identity, validity, authority, surveys, notes}) {
        innerLayout->addWidget(group);
    }
    innerLayout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
}

void CertificateEditForm::applyExpiryRule()
{
    const bool neverExpires = m_noExpiryCheck->isChecked();

    m_expiryDateEdit->setEnabled(!neverExpires);

    if (neverExpires) {
        // Cleared, not merely greyed out: the invalid combination must not
        // survive hidden behind a disabled control, ready to be saved if the
        // box is ticked and untucked again.
        m_annualCheck->setChecked(false);
        m_intermediateCheck->setChecked(false);
        m_intermediateMode->setCurrentIndex(
            m_intermediateMode->findData(static_cast<int>(IntermediateMode::Additional)));
    }

    m_annualCheck->setEnabled(!neverExpires);
    m_intermediateCheck->setEnabled(!neverExpires);

    // The mode only means anything when an intermediate survey is required.
    m_intermediateMode->setEnabled(!neverExpires && m_intermediateCheck->isChecked());
}

void CertificateEditForm::announceValidity()
{
    emit validityChanged(isValid());
}

void CertificateEditForm::setCertificate(const Certificate& certificate)
{
    m_id       = certificate.id;
    m_vesselId = certificate.vesselId;

    m_listNumberEdit->setText(certificate.listNumber);
    m_nameEdit->setText(certificate.name);
    m_categoryCombo->setCurrentIndex(
        m_categoryCombo->findData(static_cast<int>(certificate.category)));
    m_numberEdit->setText(certificate.certificateNumber);
    m_appliesToEdit->setText(certificate.appliesTo);

    if (certificate.issueDate.isValid()) {
        m_issueDateEdit->setDate(certificate.issueDate);
    }

    const bool neverExpires = certificate.neverExpires();
    m_noExpiryCheck->setChecked(neverExpires);
    if (!neverExpires) {
        m_expiryDateEdit->setDate(certificate.expiryDate);
    }
    m_interimCheck->setChecked(certificate.isInterim);

    m_issuedByEdit->setText(certificate.issuedBy);
    m_placeOfIssueEdit->setText(certificate.placeOfIssue);

    m_annualCheck->setChecked(certificate.requiresAnnualSurvey);
    m_intermediateCheck->setChecked(certificate.requiresIntermediateSurvey);
    m_intermediateMode->setCurrentIndex(
        m_intermediateMode->findData(static_cast<int>(certificate.intermediateMode)));

    m_notesEdit->setPlainText(certificate.notes);

    applyExpiryRule();
    announceValidity();
}

void CertificateEditForm::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
}

Certificate CertificateEditForm::certificate() const
{
    Certificate certificate;
    certificate.id       = m_id;
    certificate.vesselId = m_vesselId;

    certificate.listNumber = m_listNumberEdit->text().trimmed();
    certificate.name       = m_nameEdit->text().trimmed();
    certificate.category =
        (m_categoryCombo->currentIndex() < 0)
            ? CertificateCategory::Unset
            : static_cast<CertificateCategory>(m_categoryCombo->currentData().toInt());
    certificate.certificateNumber = m_numberEdit->text().trimmed();
    certificate.appliesTo         = m_appliesToEdit->text().trimmed();

    certificate.issueDate = m_issueDateEdit->date();
    // A null QDate is how "does not expire" reaches the repository.
    certificate.expiryDate = m_noExpiryCheck->isChecked() ? QDate() : m_expiryDateEdit->date();

    certificate.issuedBy     = m_issuedByEdit->text().trimmed();
    certificate.placeOfIssue = m_placeOfIssueEdit->text().trimmed();
    certificate.isInterim    = m_interimCheck->isChecked();

    certificate.requiresAnnualSurvey       = m_annualCheck->isChecked();
    certificate.requiresIntermediateSurvey = m_intermediateCheck->isChecked();
    certificate.intermediateMode =
        static_cast<IntermediateMode>(m_intermediateMode->currentData().toInt());

    certificate.notes = m_notesEdit->toPlainText().trimmed();
    return certificate;
}

bool CertificateEditForm::isValid() const
{
    return !m_nameEdit->text().trimmed().isEmpty() && m_categoryCombo->currentIndex() >= 0;
}

void CertificateEditForm::setFocusOnFirstField()
{
    m_nameEdit->setFocus();
}
