#pragma once

#include <QObject>
#include <QString>

struct Certificate;
struct CertificateState;

class CertificateAttachmentService;
class QTableWidget;

// The columns of the certificate list (certificate-list-status-spec §3, as
// amended by certificate-attachment-addendum-spec §4 and §5):
//
// No. · Name · Issue Date · Status · Expiry Date · Survey From · Survey To ·
// Days Left · Document
//
// Issue Date left this screen at step 7, in favour of a "what needs attention"
// focus, and comes back here (§5) — placed next to Expiry Date rather than at
// the end, so the two dates that answer "what is this document, and when does
// it run out" sit together instead of being separated by status and survey.
//
// Document is last (§4) so that nothing else had to be renumbered for it.
//
// Declared in this header rather than inside either .cpp, for the same reason
// CertificateListRow.h exists: this class writes the cells while
// CertificateListWidget labels, sizes and sorts them, so a disagreement about
// which column is which would be silent (housekeeping-round-spec §3.1).
enum class CertificateColumn {
    ListNumber = 0,
    Name       = 1,
    IssueDate  = 2,
    Status     = 3,
    Expiry     = 4,
    SurveyFrom = 5,
    SurveyTo   = 6,
    DaysLeft   = 7,
    Attachment = 8
};

constexpr int kCertificateColumnCount = 9;

// Fills one row of the certificate list: which cell holds what, which item
// subclass each one is, and what the Document mark looks like.
//
// Split out of CertificateListWidget for its size (housekeeping-round-spec
// §3.1), along the same seam as CertificateListActions, CertificateIoActions
// and CertificateDeleteActions. The widget keeps reload(), which gathers the
// data and calls fillRow() once per row. Nothing is computed here: the state
// arrives already calculated.
//
// A QObject so Qt's parent-child ownership deletes it, like the action
// objects beside it on the same screen.
class CertificateRowRenderer : public QObject
{
    Q_OBJECT

public:
    CertificateRowRenderer(QTableWidget&                 table,
                           CertificateAttachmentService& attachments,
                           QObject*                      parent = nullptr);

    // Which vessel's attachments to ask about. Set the same way the action
    // objects on this screen are, so there is one answer per screen rather
    // than one passed down with every row.
    void setVesselId(const QString& vesselId);

    // Renders one already-computed row. The state is passed in rather than
    // recomputed so "today" is read exactly once per reload.
    void fillRow(int row, const Certificate& certificate, const CertificateState& state);

private:
    QTableWidget&                 m_table;
    CertificateAttachmentService& m_attachments;
    QString                       m_vesselId;
};
