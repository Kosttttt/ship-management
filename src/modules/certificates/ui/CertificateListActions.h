#pragma once

#include <QDate>
#include <QObject>
#include <QString>

class CertificateAttachmentService;
class CertificateExtensionRepository;
class CertificateRepository;
class EndorsementRepository;
class QAction;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

// The certificate list's selection-driven actions: Renew
// (certificate-renewal-spec §5), the short-term extension Start/Resolve button
// (certificate-extension-spec §7), and the Attachment menu
// (certificate-attachment-spec §8).
//
// Split out of CertificateListWidget, which had grown past the ~300-line
// guideline in CLAUDE.md §9. The seam is a real one rather than a line count
// being satisfied: everything here is driven by *which row is selected*, while
// what stays behind in the widget is driven by *what the vessel's certificates
// are* — the table, the reload, and the rendering of a row. The two sides share
// only the row data in CertificateListRow.h.
//
// It owns its two buttons so that the enablement rules and the labels live
// next to the code that acts on them; the widget asks for them by accessor and
// puts them in its own layout. Object names are unchanged from when they lived
// in the widget, because that is how both the tests and MainWindow find them.
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

    // One button carrying a menu rather than three more buttons in the row
    // (certificate-attachment-spec §8): attaching is occasional, and a row of
    // permanently greyed-out buttons reads as broken.
    QPushButton* attachmentButton() const;

    // Renewal opens the edit dialog for one vessel's certificate, so the
    // actions need to know which vessel the list is currently showing. Pushed
    // down rather than read back out of the widget, which would make the two
    // classes mutually dependent.
    void setVesselId(const QString& vesselId);

    // Both buttons at once, so a selection change and a reload can never
    // update one and forget the other. Connected to the table's selection
    // here, and called again by the widget at the end of a reload — a rebuilt
    // table can drop the selection without emitting anything.
    void updateButtonStates();

signals:
    // Something was renewed, started or resolved: the list is now stale. The
    // widget answers this by reloading, which is also what refreshes the
    // button labels through updateButtonStates().
    void listChanged();

private:
    void renewCertificate();
    void startOrResolveExtension();

    // The §8 menu, in the order the user meets it: look at what is there,
    // then replace it with a file or with a whole folder.
    void openCurrentAttachment();
    void attachFile();
    void attachFolder();

    // The one place both menu entries end up, with the source already chosen.
    // sourceIsFolder comes from which entry was used rather than from asking
    // the filesystem, because that is also what decides the stored name.
    void attachFrom(const QString& sourcePath, bool sourceIsFolder);

    void updateRenewButtonState();
    void updateExtensionButtonState();
    void updateAttachmentButtonState();

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

    QTableWidget* m_table        = nullptr;
    QWidget*      m_dialogParent = nullptr;
    QString       m_vesselId;

    QPushButton* m_renewButton      = nullptr;
    QPushButton* m_extensionButton  = nullptr;
    QPushButton* m_attachmentButton = nullptr;

    // Owned by the button, which is owned by dialogParent. Kept as members
    // because the first of them is shown or hidden as the selection changes.
    QAction* m_openAttachmentAction = nullptr;
};
