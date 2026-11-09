#include "modules/certificates/ui/CertificateDeleteActions.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateBulkDeletion.h"
#include "modules/certificates/ui/CertificateSelection.h"

#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

#include <optional>

namespace {

// Above this many, the confirmation gives the count alone. Listing forty
// names turns the question into a wall of text nobody reads, which is worse
// than not listing them at all - and the count is the part that actually
// tells the user whether they selected what they meant to.
constexpr int kMaxNamesInConfirmation = 10;

// Keeps a summary from becoming the same wall of text. The count is always
// exact; only the detail lines are capped.
constexpr int kMaxLinesInSummary = 10;

QString bulletList(const QStringList& lines, int limit)
{
    QStringList shown = lines.mid(0, limit);
    for (QString& line : shown) {
        line.prepend(QStringLiteral("  - "));
    }
    QString text = shown.join(QLatin1Char('\n'));
    if (lines.size() > limit) {
        text += QLatin1Char('\n')
                + QStringLiteral("  - ...and %1 more").arg(lines.size() - limit);
    }
    return text;
}

} // namespace

CertificateDeleteActions::CertificateDeleteActions(CertificateRepository&          repository,
                                                   EndorsementRepository&          endorsements,
                                                   CertificateExtensionRepository& extensions,
                                                   CertificateAttachmentService&   attachments,
                                                   QTableWidget*                   table,
                                                   QWidget*                        dialogParent)
    : QObject(dialogParent)
    , m_repository(repository)
    , m_endorsements(endorsements)
    , m_deletion(repository, extensions, attachments)
    , m_table(table)
    , m_dialogParent(dialogParent)
{
    // addendum 2 §4. The ellipsis is not decoration: it says a question comes
    // before anything happens, which on the one destructive button in the row
    // is worth saying in the label.
    m_button = new QPushButton(tr("&Delete…"), dialogParent);
    m_button->setObjectName(QStringLiteral("deleteCertificateButton"));
    m_button->setEnabled(false);

    connect(m_button, &QPushButton::clicked, this, &CertificateDeleteActions::deleteSelected);
}

QPushButton* CertificateDeleteActions::button() const
{
    return m_button;
}

void CertificateDeleteActions::updateState()
{
    m_button->setEnabled(!CertificateSelection::allCertificateIds(m_table).isEmpty());
}

int CertificateDeleteActions::surveyCountFor(const QString& certificateId) const
{
    QList<Endorsement> endorsements;
    m_endorsements.list(certificateId, &endorsements);
    return static_cast<int>(endorsements.size());
}

QString CertificateDeleteActions::attachmentWarning() const
{
    return tr("Any document attached to it, and any short-term extension's document, "
              "will be permanently deleted from the vessel's attachment folder — not "
              "moved to the archive. This cannot be undone from inside the "
              "application.");
}

void CertificateDeleteActions::deleteSelected()
{
    const QStringList ids = CertificateSelection::allCertificateIds(m_table);
    if (ids.isEmpty()) {
        return;
    }

    // Loaded before anything is asked, so the confirmation can name real
    // certificates rather than row numbers.
    QList<Certificate> certificates;
    QStringList        unreadable;
    for (const QString& id : ids) {
        std::optional<Certificate> certificate;
        if (!m_repository.findById(id, &certificate) || !certificate.has_value()) {
            unreadable.append(id);
            continue;
        }
        certificates.append(*certificate);
    }

    if (certificates.isEmpty()) {
        // Same message the single path has always given when the one selected
        // certificate could not be read.
        QMessageBox::warning(m_dialogParent, tr("Could not open certificate"),
                             m_repository.errorString());
        return;
    }

    if (certificates.size() == 1 && unreadable.isEmpty()) {
        deleteOne(certificates.first());
        return;
    }

    deleteMany(certificates);

    // Said after the summary rather than folded into it: a row whose
    // certificate could not even be read is a different problem from one that
    // could not be deleted, and it is vanishingly rare.
    if (!unreadable.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent, tr("Some rows could not be read"),
            tr("%n selected row(s) could not be read from the database and were left alone.",
               nullptr, static_cast<int>(unreadable.size())));
    }
}

void CertificateDeleteActions::deleteOne(const Certificate& certificate)
{
    const int surveys = surveyCountFor(certificate.id);

    // §4: explicit that the real files go too, because that is the part
    // nothing else in this application does and the part that cannot be
    // undone from inside it. Named rather than described — "this certificate"
    // is not enough when the question arrives over a list of forty rows.
    QString message = tr("Delete '%1'?").arg(certificate.name);
    if (surveys > 0) {
        message += QLatin1Char('\n')
                   + tr("Its %n recorded survey(s) will be removed with it.", nullptr, surveys);
    }
    message += QStringLiteral("\n\n") + attachmentWarning();

    const auto answer = QMessageBox::question(m_dialogParent, tr("Delete certificate"), message,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return; // nothing has happened at all
    }

    if (!m_deletion.deleteCertificate(certificate)) {
        QMessageBox::warning(m_dialogParent, tr("Could not delete the certificate"),
                             m_deletion.errorString());
        return;
    }

    // Gone from the list, but a file would not go. Said out loud rather than
    // swallowed: somebody will otherwise find it in the folder later with no
    // certificate left to explain it.
    if (!m_deletion.errorString().isEmpty()) {
        QMessageBox::warning(
            m_dialogParent, tr("Deleted, but a file is still there"),
            tr("The certificate was deleted, but one of its files could not be "
               "removed.\n\n%1")
                .arg(m_deletion.errorString()));
    }

    emit listChanged();
}

void CertificateDeleteActions::deleteMany(const QList<Certificate>& certificates)
{
    const int count = static_cast<int>(certificates.size());

    int surveys = 0;
    for (const Certificate& certificate : certificates) {
        surveys += surveyCountFor(certificate.id);
    }

    QString message = tr("Delete %n selected certificate(s)?", nullptr, count);

    if (count <= kMaxNamesInConfirmation) {
        QStringList names;
        names.reserve(certificates.size());
        for (const Certificate& certificate : certificates) {
            names.append(certificate.name);
        }
        message += QStringLiteral("\n\n") + bulletList(names, kMaxNamesInConfirmation);
    }

    if (surveys > 0) {
        message += QStringLiteral("\n\n")
                   + tr("Their %n recorded survey(s) will be removed with them.", nullptr,
                        surveys);
    }
    message += QStringLiteral("\n\n") + attachmentWarning();

    const auto answer =
        QMessageBox::question(m_dialogParent, tr("Delete certificates"), message,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return; // nothing has happened at all
    }

    // §2: best effort per certificate, no outer transaction. The rule itself
    // lives in the service layer; what is left here is saying what happened.
    const BulkDeletionOutcome outcome = deleteCertificates(m_deletion, certificates);

    if (!outcome.failures.isEmpty() || !outcome.fileLeftovers.isEmpty()) {
        QString summary = tr("%n certificate(s) deleted.", nullptr,
                             static_cast<int>(outcome.deletedNames.size()));

        if (!outcome.failures.isEmpty()) {
            summary += QStringLiteral("\n\n")
                       + tr("%n could not be deleted and are still in the list:", nullptr,
                            static_cast<int>(outcome.failures.size()))
                       + QLatin1Char('\n') + bulletList(outcome.failures, kMaxLinesInSummary);
        }

        if (!outcome.fileLeftovers.isEmpty()) {
            summary += QStringLiteral("\n\n")
                       + tr("%n were deleted, but a file of theirs could not be removed from "
                            "the attachment folder:",
                            nullptr, static_cast<int>(outcome.fileLeftovers.size()))
                       + QLatin1Char('\n')
                       + bulletList(outcome.fileLeftovers, kMaxLinesInSummary);
        }

        QMessageBox::warning(m_dialogParent, tr("Deleted with problems"), summary);
    }
    // Nothing is shown when every one of them went cleanly, which is the same
    // judgment the single-certificate path has always made: the list
    // refreshing under the user is the confirmation.

    if (outcome.anythingDeleted()) {
        emit listChanged();
    }
}
