#include "modules/certificates/ui/CertificateEditDialog.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateEditForm.h"
#include "modules/certificates/ui/EndorsementEditDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString surveyTypeLabel(SurveyType type)
{
    switch (type) {
    case SurveyType::Annual:
        return CertificateEditDialog::tr("Annual");
    case SurveyType::Intermediate:
        return CertificateEditDialog::tr("Intermediate");
    case SurveyType::Initial:
        return CertificateEditDialog::tr("Initial");
    case SurveyType::Renewal:
        return CertificateEditDialog::tr("Renewal");
    case SurveyType::Unset:
        break;
    }
    return QString();
}

} // namespace

CertificateEditDialog::CertificateEditDialog(CertificateRepository&            repository,
                                             EndorsementRepository&            endorsements,
                                             const QString&                    vesselId,
                                             const std::optional<Certificate>& existing,
                                             QWidget*                          parent)
    : QDialog(parent)
    , m_repository(repository)
    , m_endorsements(endorsements)
    , m_isEditing(existing.has_value())
{
    setWindowTitle(m_isEditing ? tr("Edit Certificate") : tr("Add Certificate"));
    setModal(true);

    m_form = new CertificateEditForm(this);

    if (existing.has_value()) {
        m_form->setCertificate(*existing);
        m_certificateId        = existing->id;
        m_requiresAnnual       = existing->requiresAnnualSurvey;
        m_requiresIntermediate = existing->requiresIntermediateSurvey;
    } else {
        // Only the vessel is set: the form keeps its own defaults for
        // everything else, including an expiry date five years out rather
        // than "does not expire".
        m_form->setVesselId(vesselId);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton    = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(m_isEditing ? tr("Save") : tr("Add"));

    auto* layout = new QVBoxLayout(this);
    // Stretch 1: the scrolling form takes the surplus height, so the
    // endorsements section below it does not squeeze the fields into a sliver.
    layout->addWidget(m_form, 1);

    // The section decides once, from the saved certificate, not from the
    // checkboxes as they are being edited: a requirement the user has just
    // ticked but not yet saved has nothing to attach an endorsement to.
    if (m_isEditing && (m_requiresAnnual || m_requiresIntermediate)) {
        layout->addWidget(buildEndorsementsSection(*existing), 0);
    }

    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CertificateEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &CertificateEditDialog::reject);

    connect(m_form, &CertificateEditForm::validityChanged, m_okButton, &QPushButton::setEnabled);
    m_okButton->setEnabled(m_form->isValid());

    m_form->setFocusOnFirstField();
    resize(540, m_isEditing ? 760 : 620);
}

QList<SurveyType> CertificateEditDialog::allowedSurveyTypes() const
{
    QList<SurveyType> types;
    if (m_requiresAnnual) {
        types.append(SurveyType::Annual);
    }
    if (m_requiresIntermediate) {
        types.append(SurveyType::Intermediate);
    }
    return types;
}

QGroupBox* CertificateEditDialog::buildEndorsementsSection(const Certificate& certificate)
{
    Q_UNUSED(certificate)

    auto* group = new QGroupBox(tr("Endorsements"), this);
    group->setObjectName(QStringLiteral("endorsementsSection"));

    m_endorsementTable = new QTableWidget(group);
    m_endorsementTable->setObjectName(QStringLiteral("endorsementTable"));
    m_endorsementTable->setColumnCount(3);
    m_endorsementTable->setHorizontalHeaderLabels({tr("Type"), tr("Date"), tr("Place")});
    m_endorsementTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_endorsementTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_endorsementTable->verticalHeader()->setVisible(false);
    m_endorsementTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_endorsementTable->setMaximumHeight(120);
    m_endorsementTable->setMinimumHeight(90);

    auto* addButton = new QPushButton(tr("Add &Endorsement"), group);
    addButton->setObjectName(QStringLiteral("addEndorsementButton"));

    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(m_endorsementTable);
    groupLayout->addWidget(addButton, 0, Qt::AlignLeft);

    connect(addButton, &QPushButton::clicked, this, &CertificateEditDialog::addEndorsement);

    reloadEndorsements();
    return group;
}

void CertificateEditDialog::reloadEndorsements()
{
    if (m_endorsementTable == nullptr) {
        return;
    }

    QList<Endorsement> endorsements;
    if (!m_endorsements.list(m_certificateId, &endorsements)) {
        qWarning() << "Could not load endorsements:" << m_endorsements.errorString();
        return;
    }

    m_endorsementTable->setRowCount(endorsements.size());
    for (int row = 0; row < endorsements.size(); ++row) {
        const Endorsement& endorsement = endorsements.at(row);
        m_endorsementTable->setItem(row, 0,
                                    new QTableWidgetItem(surveyTypeLabel(endorsement.surveyType)));
        m_endorsementTable->setItem(
            row, 1,
            new QTableWidgetItem(
                endorsement.endorsementDate.toString(QStringLiteral("dd MMM yyyy"))));
        m_endorsementTable->setItem(row, 2, new QTableWidgetItem(endorsement.place));
    }
}

void CertificateEditDialog::addEndorsement()
{
    EndorsementEditDialog dialog(m_endorsements, m_certificateId, allowedSurveyTypes(), this);
    if (dialog.exec() == QDialog::Accepted) {
        reloadEndorsements();
    }
}

void CertificateEditDialog::accept()
{
    // Enter reaches QDialog::accept() directly even when OK is disabled — the
    // trap found in the first-run wizard in step 3.
    if (!m_form->isValid()) {
        return;
    }

    const Certificate edited = m_form->certificate();

    const bool saved = m_isEditing ? m_repository.update(edited) : m_repository.create(edited);

    if (!saved) {
        qWarning() << "Certificate save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    QDialog::accept();
}
