#include "modules/certificates/ui/CertificateEditDialog.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/AttachmentSourceField.h"
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
                                             CertificateAttachmentService&     attachments,
                                             const QString&                    vesselId,
                                             const std::optional<Certificate>& existing,
                                             bool                              isRenewal,
                                             QWidget*                          parent)
    : QDialog(parent)
    , m_repository(repository)
    , m_endorsements(endorsements)
    , m_attachments(attachments)
    , m_isEditing(existing.has_value())
    , m_isRenewal(isRenewal && existing.has_value())
{
    // Naming the certificate in the renewal title so the two modes can never
    // be confused — one of them clears the survey history.
    if (m_isRenewal) {
        setWindowTitle(tr("Renew — %1").arg(existing->name));
    } else {
        setWindowTitle(m_isEditing ? tr("Edit Certificate") : tr("Add Certificate"));
    }
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
    m_okButton->setText(m_isRenewal ? tr("Renew") : (m_isEditing ? tr("Save") : tr("Add")));

    auto* layout = new QVBoxLayout(this);
    // Stretch 1: the scrolling form takes the surplus height, so the
    // endorsements section below it does not squeeze the fields into a sliver.
    layout->addWidget(m_form, 1);

    // The section decides once, from the saved certificate, not from the
    // checkboxes as they are being edited: a requirement the user has just
    // ticked but not yet saved has nothing to attach an endorsement to.
    //
    // Never shown during a renewal: a survey stamp added here would belong to
    // neither cycle — not the old one, which is about to be cleared, and not
    // the new one, which has no anniversaries until the new dates are saved
    // (certificate-renewal-spec §6).
    if (m_isEditing && !m_isRenewal && (m_requiresAnnual || m_requiresIntermediate)) {
        layout->addWidget(buildEndorsementsSection(*existing), 0);
    }

    // certificate-attachment-addendum-spec §3: offered in all three modes.
    // Step 12 showed it on renewal only, on the reasoning that a renewal is
    // when a new document physically arrives; manual testing made the simpler
    // point that the document is just as often in hand when the certificate is
    // first entered.
    layout->addWidget(buildAttachmentSection(), 0);

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

QGroupBox* CertificateEditDialog::buildAttachmentSection()
{
    auto* group = new QGroupBox(tr("Attached document"), this);
    group->setObjectName(QStringLiteral("attachmentSection"));

    // Worded per mode (addendum §3). Edit and Renewal are both worded so that
    // doing nothing is obviously safe — the commonest renewal is one where the
    // scan arrives later, and neither dialog may imply that skipping this
    // loses a document that is already there. On Add there is nothing to lose,
    // and saying there is would be a small lie.
    const QString emptyText = m_isEditing
                                  ? tr("The document already attached stays as it is.")
                                  : tr("No document is attached yet.");

    m_attachmentSource = new AttachmentSourceField(emptyText, group);

    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(m_attachmentSource);

    return group;
}

void CertificateEditDialog::replaceAttachmentIfChosen(const Certificate& saved)
{
    if (m_attachmentSource == nullptr || m_attachmentSource->sourcePath().isEmpty()) {
        return; // left alone: nothing about the attachment changes
    }

    // saved.expiryDate is the right date in all three modes, because it comes
    // from the row that was just written: the new date on a renewal, and the
    // unchanged one on an ordinary edit or an addition (§3).
    if (!m_attachments.replaceCurrent(saved, saved.expiryDate,
                                      m_attachmentSource->sourcePath(),
                                      m_attachmentSource->sourceIsFolder())) {
        qWarning() << "Could not attach the document to this certificate:"
                   << m_attachments.errorString();
        QMessageBox::warning(
            this, tr("Saved, but the document was not attached"),
            tr("The certificate was saved, but the document could not be attached to "
               "it.\n\n%1")
                .arg(m_attachments.errorString()));
        // Still accept, exactly as a failed survey clear does: the certificate
        // itself was written, and keeping the dialog open would invite a second
        // write of data already stored.
    }
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

    Certificate edited = m_form->certificate();

    // The generated id is asked for, because on Add it is the only way to name
    // what was just created — and the attachment below has to be attached to
    // something (addendum §3).
    QString    createdId;
    const bool saved = m_isEditing ? m_repository.update(edited)
                                   : m_repository.create(edited, &createdId);

    if (!saved) {
        qWarning() << "Certificate save failed:" << m_repository.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_repository.errorString()));
        return; // stay open, data intact
    }

    // certificate-renewal-spec §4: the completed cycle's surveys do not
    // describe the new one, so they are cleared once the new dates are
    // safely stored.
    //
    // Deliberately not one transaction with the update above: both are single
    // unconditional UPDATEs against a local file, and the consequence of the
    // unlikely half-failure is bounded — the certificate is correctly
    // renewed, and endorsements predating the new cycle's first window are
    // skipped by the matching rule anyway. The error is reported rather than
    // swallowed.
    if (m_isRenewal && !m_endorsements.deleteAllForCertificate(m_certificateId)) {
        qWarning() << "Renewal could not clear recorded surveys:"
                   << m_endorsements.errorString();
        QMessageBox::warning(
            this,
            tr("Renewed, but surveys were not cleared"),
            tr("The certificate's new dates were saved, but its recorded surveys could not "
               "be cleared.\n\n%1")
                .arg(m_endorsements.errorString()));
        // Still accept: the renewal itself succeeded, and leaving the dialog
        // open would invite a second update of data already written.
    }

    // After the write, per §8 and addendum §3: the filesystem work is not
    // worth doing if the row never made it into the database.
    //
    // On Edit and Renewal `edited` already carries the id the form was loaded
    // with. On Add it carries none, so the id the repository has just
    // generated is put on it here — the row exists now, and that is what makes
    // naming a file after it possible at all.
    if (!m_isEditing) {
        edited.id = createdId;
    }
    replaceAttachmentIfChosen(edited);

    QDialog::accept();
}
