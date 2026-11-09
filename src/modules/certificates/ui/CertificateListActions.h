#pragma once

#include <QDate>
#include <QObject>
#include <QString>

class CertificateAttachmentActions;
class CertificateAttachmentService;
class CertificateDeleteActions;
class CertificateExtensionRepository;
class CertificateRepository;
class EndorsementRepository;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

// The certificate list's selection-driven actions: Renew
// (certificate-renewal-spec §5), the short-term extension Start/Resolve button
// (certificate-extension-spec §7), and Delete Certificate
// (certificate-attachment-addendum-2-spec §4).
//
// Split out of CertificateListWidget, which had grown past the ~300-line
// guideline in CLAUDE.md §9. The seam is a real one rather than a line count
// being satisfied: everything here is driven by *which row is selected*, while
// what stays behind in the widget is driven by *what the vessel's certificates
// are* — the table, the reload, and the rendering of a row. The two sides share
// only the row data in CertificateListRow.h.
//
// The Attachment button and its menu were split out again, into
// CertificateAttachmentActions, when addendum 2 added a fourth entry to it.
// This class still owns that object and still hands its button on, so the
// widget laying the row out never had to learn about the change.
//
// Delete went the same way in turn, into CertificateDeleteActions, when
// certificate-list-usability-fixes.md §2 taught it to act on a whole
// selection: a plural confirmation, a best-effort loop and a combined summary
// would have put this file back over the guideline on its own. Delete is also
// now the only action here that is not about exactly one certificate, which
// makes it the odd one out rather than one of a set.
//
// It owns its buttons so that the enablement rules and the labels live next to
// the code that acts on them; the widget asks for them by accessor and puts
// them in its own layout. Object names are unchanged from when they lived in
// the widget, because that is how both the tests and MainWindow find them.
class CertificateListActions : public QObject
{
    Q_OBJECT

public:
    // Parented to dialogParent, which is both the QObject owner and the window
    // every dialog and message box raised from here is centred on.
    CertificateListActions(CertificateRepository&          repository,
                           EndorsementRepository&          endorsements,
                           CertificateExtensionRepository& extensions,
                           CertificateAttachmentService&   attachments,
                           QTableWidget*                   table,
                           QWidget*                        dialogParent);

    QPushButton* renewButton() const;
    QPushButton* extensionButton() const;

    // Owned by CertificateDeleteActions. Last in the row, and the only button
    // here that destroys anything, which is why it asks first and says what it
    // is about to do (addendum 2 §4). The one action that works on a whole
    // selection rather than on one certificate (usability-fixes §2).
    QPushButton* deleteButton() const;

    // Owned by CertificateAttachmentActions, passed through so that the widget
    // places one row of buttons without knowing which object owns which.
    QPushButton* attachmentButton() const;

    // Renewal opens the edit dialog for one vessel's certificate, so the
    // actions need to know which vessel the list is currently showing. Pushed
    // down rather than read back out of the widget, which would make the two
    // classes mutually dependent.
    void setVesselId(const QString& vesselId);

    // Every button at once, so a selection change and a reload can never
    // update one and forget the others. Connected to the table's selection
    // here, and called again by the widget at the end of a reload — a rebuilt
    // table can drop the selection without emitting anything.
    void updateButtonStates();

signals:
    // Something was renewed, started, resolved, attached or deleted: the list
    // is now stale. The widget answers this by reloading, which is also what
    // refreshes the button labels through updateButtonStates().
    void listChanged();

private:
    void renewCertificate();
    void startOrResolveExtension();

    void updateRenewButtonState();
    void updateExtensionButtonState();

    // The three accessors below all ask CertificateSelection, which is where
    // the multi-select rule lives: "more than one row selected" reports no
    // selection at all, exactly as "none" does
    // (certificate-list-usability-fixes.md §2). That one rule is what
    // disables Renew and the extension button the moment a second row is
    // picked, with nothing else here needing to change - both actions are
    // inherently about one certificate.
    //
    // Kept as named members rather than call sites reaching into the
    // namespace directly, because the names are what make the rules below
    // readable.

    // The first cell of the single selected row — where all the row data
    // lives — or null when none, or more than one, is selected.
    QTableWidgetItem* selectedDataItem() const;

    // The id on that row, or empty on the same terms.
    QString selectedCertificateId() const;

    // The active extension's expiry on that row, or an invalid date when
    // there is none.
    QDate selectedExtensionExpiry() const;

    CertificateRepository&          m_repository;
    EndorsementRepository&          m_endorsements;
    CertificateExtensionRepository& m_extensions;
    CertificateAttachmentService&   m_attachments;

    QTableWidget* m_table        = nullptr;
    QWidget*      m_dialogParent = nullptr;
    QString       m_vesselId;

    QPushButton* m_renewButton     = nullptr;
    QPushButton* m_extensionButton = nullptr;

    // Both QObject children of dialogParent, like this class itself. Each
    // owns its own button and the rules for enabling it; this class only
    // passes those buttons on and forwards their listChanged().
    CertificateAttachmentActions* m_attachmentActions = nullptr;
    CertificateDeleteActions*     m_deleteActions     = nullptr;
};
