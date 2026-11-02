#pragma once

#include "modules/certificates/service/CertificateDeletionService.h"

#include <QDate>
#include <QObject>
#include <QString>

class CertificateAttachmentActions;
class CertificateAttachmentService;
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

    // addendum 2 §4. Last in the row, and the only button here that destroys
    // anything, which is why it asks first and says what it is about to do.
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
    void deleteCertificate();

    void updateRenewButtonState();
    void updateExtensionButtonState();
    void updateDeleteButtonState();

    // The selected row, or -1. Based on the selection rather than the
    // "current" focus cell, which Qt can set without anything appearing
    // selected.
    int selectedRow() const;

    // The first cell of the selected row — where all the row data lives — or
    // null when there is no selection.
    QTableWidgetItem* selectedDataItem() const;

    // The id on the row currently selected, or empty when there is none.
    QString selectedCertificateId() const;

    // The active extension's expiry on the selected row, or an invalid date
    // when there is no selection or no extension.
    QDate selectedExtensionExpiry() const;

    CertificateRepository&          m_repository;
    EndorsementRepository&          m_endorsements;
    CertificateExtensionRepository& m_extensions;
    CertificateAttachmentService&   m_attachments;

    // A value member rather than something handed in: it is built from
    // exactly the collaborators this class already holds, so plumbing it down
    // from the module and through the list widget would add two parameters
    // and tell nobody anything.
    CertificateDeletionService m_deletion;

    QTableWidget* m_table        = nullptr;
    QWidget*      m_dialogParent = nullptr;
    QString       m_vesselId;

    QPushButton* m_renewButton     = nullptr;
    QPushButton* m_extensionButton = nullptr;
    QPushButton* m_deleteButton    = nullptr;

    // A QObject child of dialogParent, like this class itself.
    CertificateAttachmentActions* m_attachmentActions = nullptr;
};
