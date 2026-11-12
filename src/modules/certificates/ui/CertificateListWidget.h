#pragma once

#include "modules/certificates/service/CertificateListRows.h"

#include <QWidget>

struct Certificate;
struct CertificateState;

class AppSettingRepository;
class CertificateAttachmentService;
class CertificateCsvExporter;
class CertificateCsvImporter;
class CertificateCategoryTabs;
class CertificateExtensionRepository;
class CertificateIoActions;
class CertificateListActions;
class CertificateReportBuilder;
class CertificateRowRenderer;
class CertificateRepository;
class TableHeaderSettings;
class EndorsementRepository;
class InstallationContext;
class QCheckBox;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;

// The certificate list for one vessel (certificate-crud-spec §5).
//
// Unlike vessels, this screen is not split in two by installation mode. A
// vessel in VESSEL mode is inherently singular; its certificates are not — one
// ship carries dozens. So both modes use this same widget, and the only
// difference is whether MainWindow puts a vessel selector above it.
//
// This class owns the table, gathers what goes into it, and places the
// buttons. CertificateRowRenderer fills each row; the two selection-driven
// actions — Renew and the short-term extension button — live in
// CertificateListActions, which owns their buttons and asks for a reload when
// it has changed something.
class CertificateListWidget : public QWidget
{
    Q_OBJECT

public:
    CertificateListWidget(CertificateRepository&          repository,
                          EndorsementRepository&          endorsements,
                          CertificateExtensionRepository& extensions,
                          CertificateAttachmentService&   attachments,
                          AppSettingRepository&           appSettings,
                          CertificateCsvExporter&         csvExporter,
                          CertificateCsvImporter&         csvImporter,
                          CertificateReportBuilder&       reportBuilder,
                          const InstallationContext&      installation,
                          QWidget*                        parent = nullptr);

    // OFFICE: called by MainWindow when the toolbar selector changes. An empty
    // id returns the screen to the "select a vessel" prompt.
    // VESSEL: called once at construction and never again.
    void setVesselId(const QString& vesselId);

    QString vesselId() const;

    // Turns the "needs attention" filter on or off from outside — the banner's
    // drill-down uses it (alerts-spec.md §8). QCheckBox::setChecked() only
    // emits toggled when the value actually changes, so this is a no-op when
    // the filter is already in the requested state, and reload() does not run
    // twice.
    void setNeedsAttentionFilter(bool on);

    void reload();

signals:
    // Emitted at the end of a successful reload(), so the sidebar badge can
    // follow anything that could plausibly have changed the count — a vessel
    // switch, the filter toggling, or a certificate or endorsement dialog
    // closing (alerts-spec.md §6).
    void certificatesChanged();

private:
    // Add is not selection-driven — it needs only a vessel — and a
    // double-click carries the row it was aimed at, so neither of these
    // belongs with the selection-driven actions.
    void addCertificate();
    void editCertificate(QTableWidgetItem* item);

    // certificate-attachment-addendum-spec §4. A single click in the Document
    // column opens that row's attached document; a click anywhere else does
    // nothing, and double-click keeps opening Edit Certificate everywhere
    // including this column.
    //
    // Here rather than in CertificateListActions because it is driven by which
    // cell was clicked rather than by what is selected — the same distinction
    // that already keeps Add Certificate out of that class.
    void openAttachmentIfDocumentColumn(int row, int column);

    void updateVisibleState();

    CertificateRepository&          m_repository;
    EndorsementRepository&          m_endorsements;
    CertificateExtensionRepository& m_extensions;
    CertificateAttachmentService&   m_attachments;
    QString                         m_vesselId;

    // Decides what goes in the rows, a layer down: CLAUDE.md §4 rule 3 keeps
    // business rules out of a widget (housekeeping-round-spec §3.2). Held by
    // value because it is this screen's own, not shared with anything.
    CertificateListRows             m_rowSource;

    // A QObject child of this widget, so Qt's parent-child ownership deletes
    // it (CLAUDE.md §9) — and it must be constructed after m_table, which it
    // watches for selection changes.
    CertificateListActions* m_actions   = nullptr;
    CertificateIoActions*   m_ioActions = nullptr;

    // Which cell of a row holds what (housekeeping-round-spec §3.1). Also a
    // QObject child of this widget, and also built after m_table.
    CertificateRowRenderer* m_rows      = nullptr;

    // The category filter above the table (certificate-category-tabs-spec).
    // Another QObject child; it draws the tabs and reports which one is
    // picked, and decides nothing about what they contain.
    CertificateCategoryTabs* m_categoryTabs = nullptr;

    // Remembers the column widths the user drags, under its own QSettings
    // key (certificate-list-usability-fixes.md §1). A QObject child of this
    // widget, like the two action objects above.
    TableHeaderSettings*    m_headerSettings = nullptr;

    QStackedWidget* m_stack               = nullptr;
    QTableWidget*   m_table               = nullptr;
    QPushButton*    m_addButton           = nullptr;
    QCheckBox*      m_needsAttentionCheck = nullptr;
};
