#pragma once

#include "modules/certificates/domain/Endorsement.h"

#include <QGroupBox>
#include <QList>

#include <optional>

class EndorsementRepository;
class QPushButton;
class QTableWidget;

// The endorsements a certificate has recorded, inside the certificate's own
// edit dialog: a small table and the three things that can be done to it —
// add, correct, remove (certificate-endorsement-spec §6, extended by
// certificate-attachment-addendum-2-spec §6).
//
// Split out of CertificateEditDialog, which had grown past the ~300-line
// guideline in CLAUDE.md §9 and would have gone further with edit and delete.
// The seam is a real one: this knows about endorsements and nothing about
// certificate fields, dates, attachments or saving a certificate, and
// CertificateEditDialog now only has to decide whether to show it at all.
//
// Three buttons rather than a context menu. The spec left the mechanism open;
// a right-click menu hides two of the three ways in, which on a section this
// small is worse for a user meeting it for the first time, and it also cannot
// be driven from a test without synthesising a context-menu event. Both
// reasons point the same way.
class EndorsementSection : public QGroupBox
{
    Q_OBJECT

public:
    // allowedTypes are the survey types this certificate requires, passed
    // straight through to the dialog this section opens.
    EndorsementSection(EndorsementRepository&   repository,
                       const QString&           certificateId,
                       const QList<SurveyType>& allowedTypes,
                       QWidget*                 parent = nullptr);

    // Re-reads the table from the database. Public so a test can assert on
    // what the section shows after writing a row behind its back.
    void reload();

private:
    void addEndorsement();
    void editEndorsement();
    void deleteEndorsement();

    void updateButtonStates();

    // The selected row's endorsement, or std::nullopt when nothing is
    // selected. Read back from the table's own row data rather than from a
    // remembered index, which a sort or a reload would invalidate.
    std::optional<Endorsement> selectedEndorsement() const;

    EndorsementRepository& m_repository;
    QString                m_certificateId;
    QList<SurveyType>      m_allowedTypes;

    QTableWidget* m_table        = nullptr;
    QPushButton*  m_addButton    = nullptr;
    QPushButton*  m_editButton   = nullptr;
    QPushButton*  m_deleteButton = nullptr;

    // The rows as last loaded, in table order. The table shows text; an edit
    // needs every field, including the ones no column displays, so the loaded
    // structs are kept rather than reconstructed from cells.
    QList<Endorsement> m_endorsements;
};
