#pragma once

#include <QObject>
#include <QString>

class CertificateAttachmentService;
class CertificateRepository;
class QAction;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

// The certificate list's Attachment button and its menu
// (certificate-attachment-spec §8, extended by
// certificate-attachment-addendum-2-spec §5 with Delete Attachment).
//
// Split out of CertificateListActions, which had grown past the ~300-line
// guideline in CLAUDE.md §9 and would have gone further still with two new
// destructive actions. The seam is a real one: everything here is about the
// *document* on the selected row — looking at it, replacing it, removing it —
// while what stays behind in CertificateListActions is about the certificate's
// own lifecycle: renewing it, extending it, deleting it. The two share only
// the row data in CertificateListRow.h and the signal that says the list is
// now stale.
//
// It owns its button so that the enablement rules and the menu's entries live
// next to the code that acts on them; CertificateListActions asks for the
// button by accessor and passes it on to the widget that lays it out. The
// object names are unchanged from when all of this lived in one class, because
// that is how both the tests and the screens find them.
class CertificateAttachmentActions : public QObject
{
    Q_OBJECT

public:
    CertificateAttachmentActions(CertificateRepository&        repository,
                                 CertificateAttachmentService& attachments,
                                 QTableWidget*                 table,
                                 QWidget*                      dialogParent);

    // One button carrying a menu rather than four more buttons in the row
    // (§8): attaching is occasional, and a row of permanently greyed-out
    // buttons reads as broken.
    QPushButton* button() const;

    // Which vessel the list is currently showing. Pushed down rather than read
    // back out of the widget, which would make the two classes mutually
    // dependent.
    void setVesselId(const QString& vesselId);

    // Called on a selection change and again at the end of a reload — a
    // rebuilt table can drop the selection without emitting anything.
    void updateState();

signals:
    // Something was attached or deleted: the list is now stale.
    void listChanged();

private:
    // The §8 menu, in the order the user meets it: look at what is there,
    // replace it with a file or with a whole folder, or take it away.
    void openCurrentAttachment();
    void attachFile();
    void attachFolder();
    void deleteAttachment();

    // The one place both attach entries end up, with the source already
    // chosen. sourceIsFolder comes from which entry was used rather than from
    // asking the filesystem, because that is also what decides the stored
    // name.
    void attachFrom(const QString& sourcePath, bool sourceIsFolder);

    // The single selected row's certificate id, or empty when nothing is
    // selected — or when more than one row is (usability-fixes §2).
    QString selectedCertificateId() const;

    CertificateRepository&        m_repository;
    CertificateAttachmentService& m_attachments;

    QTableWidget* m_table        = nullptr;
    QWidget*      m_dialogParent = nullptr;
    QString       m_vesselId;

    QPushButton* m_button = nullptr;

    // Owned by the button's menu, which is owned by the button. Kept as
    // members because both are shown or hidden as the selection changes.
    QAction* m_openAction   = nullptr;
    QAction* m_deleteAction = nullptr;
};
