#pragma once

#include "modules/certificates/service/CertificateDeletionService.h"

#include <QObject>
#include <QString>

class CertificateAttachmentService;
class CertificateExtensionRepository;
class CertificateRepository;
class EndorsementRepository;
class QPushButton;
class QTableWidget;
class QWidget;

// The certificate list's Delete button
// (certificate-attachment-addendum-2-spec §4, extended to whole selections by
// certificate-list-usability-fixes.md §2).
//
// Split out of CertificateListActions along exactly the seam
// CertificateAttachmentActions was split out along, and for the same stated
// reason: that class was at the ~300-line guideline in CLAUDE.md §9, and
// teaching Delete to work on a selection rather than a row roughly doubled
// it. The split is a real one rather than a line count being satisfied -
// Delete is now the only action on this screen that acts on more than one
// certificate, so it is the only one that needs a plural confirmation, a
// best-effort loop and a combined summary. Everything left behind in
// CertificateListActions is still strictly single-certificate.
//
// It owns its button, so the enablement rule lives next to the code that acts
// on it; CertificateListActions still hands that button on, under the object
// name it has always had, so neither the widget placing it nor the tests
// finding it noticed the move.
class CertificateDeleteActions : public QObject
{
    Q_OBJECT

public:
    // Parented to dialogParent, which is both the QObject owner and the
    // window every message box raised from here is centred on.
    CertificateDeleteActions(CertificateRepository&          repository,
                             EndorsementRepository&          endorsements,
                             CertificateExtensionRepository& extensions,
                             CertificateAttachmentService&   attachments,
                             QTableWidget*                   table,
                             QWidget*                        dialogParent);

    QPushButton* button() const;

    // Enabled whenever anything at all is selected. No never-expires rule and
    // no extension rule, unlike Renew and the extension button: any
    // certificate can be entered by mistake, including one that never expires
    // and one currently extended (§4).
    void updateState();

signals:
    // At least one certificate was removed, so the list is stale.
    void listChanged();

private:
    void deleteSelected();

    // Exactly one selected: the original single-certificate flow, wording and
    // all, kept deliberately unchanged (§2).
    void deleteOne(const Certificate& certificate);

    // More than one: one confirmation naming the count, then the loop, then
    // one combined summary instead of a dialog per row.
    void deleteMany(const QList<Certificate>& certificates);

    // The paragraph both confirmations end with. Identical wording for one
    // certificate and for twenty, because the consequence is identical.
    QString attachmentWarning() const;

    // Best-effort count for a confirmation: an unreadable endorsement list
    // makes the question less specific but must never block the deletion.
    int surveyCountFor(const QString& certificateId) const;

    CertificateRepository& m_repository;
    EndorsementRepository& m_endorsements;

    // A value member built from the collaborators handed in, the same way
    // CertificateListActions used to hold it: plumbing it down from the module
    // would add three parameters and tell nobody anything.
    CertificateDeletionService m_deletion;

    QTableWidget* m_table        = nullptr;
    QWidget*      m_dialogParent = nullptr;
    QPushButton*  m_button       = nullptr;
};
