#include "modules/certificates/ui/CertificateListWidget.h"

#include "core/InstallationContext.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateIoActions.h"
#include "modules/certificates/ui/CertificateListActions.h"
#include "modules/certificates/ui/CertificateListRow.h"
#include "modules/certificates/ui/CertificateCategoryTabs.h"
#include "modules/certificates/ui/CertificateRowRenderer.h"
#include "modules/certificates/ui/TableHeaderSettings.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Laid out by CertificateRowRenderer, which declares them — this screen only
// labels, sizes and sorts them (housekeeping-round-spec §3.1).
using Column = CertificateColumn;

constexpr int kColumnCount = kCertificateColumnCount;

// usability-fixes §1: the widths a user sees before they drag anything, in
// Column order. Name is deliberately far wider than the rest - it is the one
// column that routinely holds something long ("International Oil Pollution
// Prevention Certificate"), and the old equal-share Stretch mode is exactly
// what clipped it. Expiry Date is the second widest because an active
// short-term extension writes a date plus a suffix into that one cell.
//
// Starting points, not settings: whatever the user drags these to is what
// they get next time (TableHeaderSettings).
constexpr int kDefaultColumnWidths[kColumnCount] = {
    64,  // No.
    320, // Name
    104, // Issue Date
    124, // Status
    176, // Expiry Date
    104, // Survey From
    104, // Survey To
    80,  // Days Left
    84   // Document
};

// Which page of the internal stack is showing. Named so the tests can assert
// the prompt state rather than guessing from an empty table.
enum class Page {
    Prompt = 0,
    List   = 1
};

} // namespace

CertificateListWidget::CertificateListWidget(CertificateRepository&          repository,
                                             EndorsementRepository&          endorsements,
                                             CertificateExtensionRepository& extensions,
                                             CertificateAttachmentService&   attachments,
                                             AppSettingRepository&           appSettings,
                                             CertificateCsvExporter&         csvExporter,
                                             CertificateCsvImporter&         csvImporter,
                                             CertificateReportBuilder&       reportBuilder,
                                             const InstallationContext&      installation,
                                             QWidget*                        parent)
    : QWidget(parent)
    , m_repository(repository)
    , m_endorsements(endorsements)
    , m_extensions(extensions)
    , m_attachments(attachments)
    , m_rowSource(repository, endorsements, extensions, appSettings)
{
    m_addButton = new QPushButton(tr("&Add Certificate"), this);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("certificateTable"));
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels({tr("No."), tr("Name"), tr("Issue Date"), tr("Status"),
                                        tr("Expiry Date"), tr("Survey From"), tr("Survey To"),
                                        tr("Days Left"), tr("Document")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // usability-fixes §2: ordinary Ctrl/Shift-click multi-select, so a batch
    // of certificates entered by mistake can be cleaned up from inside the
    // application. Only Delete acts on more than one; every other action here
    // stays single-certificate, which CertificateListActions enforces by
    // treating "more than one selected" as no selection at all.
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // edited via the dialog
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    // usability-fixes §1: Interactive, not Stretch or ResizeToContents. Both
    // of those actively disable dragging a column border, and Stretch divides
    // the width equally regardless of what a column holds, which clipped long
    // certificate names with no way to see the rest.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // The last column absorbs whatever width is left over, so widening the
    // window does not leave dead space to the right of Document.
    m_table->horizontalHeader()->setStretchLastSection(true);

    // Starting widths only - the user drags from here, and whatever they drag
    // to is remembered (below). Name gets by far the most, because it is the
    // one column that routinely needs it.
    for (int column = 0; column < kColumnCount; ++column) {
        m_table->horizontalHeader()->resizeSection(column, kDefaultColumnWidths[column]);
    }

    // The reference number is the whole point of the field, so it is the sort
    // a user gets without clicking anything (certificate-crud-spec §8.2).
    // Any header click still re-sorts by that column, as before.
    m_table->sortByColumn(static_cast<int>(Column::ListNumber), Qt::AscendingOrder);

    // Last, so a returning user's own layout wins over the defaults just set
    // (§1). Saving starts only after restoring, or restoring would fire the
    // header's own signals and write a half-applied layout straight back.
    m_headerSettings = new TableHeaderSettings(
        m_table->horizontalHeader(), QStringLiteral("ui/certificateList/headerState"), this);
    m_headerSettings->restore();
    m_headerSettings->startSaving();

    // Also after the table, which it writes into. It owns which cell holds
    // what; reload() below owns what the values are
    // (housekeeping-round-spec §3.1).
    m_rows = new CertificateRowRenderer(*m_table, m_attachments, this);

    // The tab bar owns its own labels and colours; what the numbers are comes
    // back from the service with the rows (category-tabs-spec §4).
    m_categoryTabs = new CertificateCategoryTabs(this);

    // After the table, which it watches for selection changes. It owns the
    // Renew, extension and attachment buttons; this widget only places them.
    m_actions = new CertificateListActions(m_repository, m_endorsements, m_extensions,
                                           m_attachments, m_table, this);

    // Import and export are driven by which vessel is on screen rather than
    // by which row is selected, which is why they are their own object
    // instead of three more buttons on CertificateListActions
    // (certificate-import-export-spec §1). The services themselves are owned
    // by the module: a widget never holds a database connection.
    m_ioActions = new CertificateIoActions(csvExporter, csvImporter, reportBuilder, this);

    auto* hint = new QLabel(tr("Double-click a certificate to edit it."), this);
    hint->setEnabled(false); // renders as secondary text

    // certificate-list-status-spec §7. Unchecked by default: the list shows
    // everything until asked otherwise.
    m_needsAttentionCheck = new QCheckBox(tr("Show only certificates needing attention"), this);
    m_needsAttentionCheck->setObjectName(QStringLiteral("needsAttentionCheck"));

    auto* controlRow = new QHBoxLayout;
    controlRow->setContentsMargins(0, 0, 0, 0);
    controlRow->addWidget(m_addButton);
    controlRow->addWidget(m_actions->renewButton());
    controlRow->addWidget(m_actions->extensionButton());
    controlRow->addWidget(m_actions->attachmentButton());
    // Last of the action buttons and furthest from Add, which is the one it
    // would do the most damage to be mistaken for
    // (certificate-attachment-addendum-2-spec §4).
    controlRow->addWidget(m_actions->deleteButton());
    controlRow->addStretch();
    controlRow->addWidget(m_needsAttentionCheck);

    // A row of their own rather than a ninth button beside Delete: these
    // three act on the whole vessel, and mixing them in with the buttons that
    // act on one selected certificate would invite exactly the wrong click.
    auto* ioRow = new QHBoxLayout;
    ioRow->setContentsMargins(0, 0, 0, 0);
    ioRow->addWidget(m_ioActions->importCsvButton());
    ioRow->addWidget(m_ioActions->exportCsvButton());
    ioRow->addWidget(m_ioActions->exportPdfButton());
    ioRow->addStretch();

    auto* listPage       = new QWidget(this);
    auto* listPageLayout = new QVBoxLayout(listPage);
    listPageLayout->setContentsMargins(0, 0, 0, 0);
    listPageLayout->addLayout(controlRow);
    listPageLayout->addLayout(ioRow);
    // Directly above the table (category-tabs-spec §1), and below the rows of
    // buttons: the buttons act on a certificate or the vessel, while the tabs
    // decide which certificates the table is showing.
    listPageLayout->addWidget(m_categoryTabs->bar());
    listPageLayout->addWidget(m_table);
    listPageLayout->addWidget(hint);

    // An explicit prompt, not an empty table: an empty table would look like
    // "this vessel has no certificates", which is a different fact.
    auto* prompt = new QLabel(tr("Select a vessel above to see its certificates."), this);
    prompt->setObjectName(QStringLiteral("selectVesselPrompt"));
    prompt->setAlignment(Qt::AlignCenter);
    prompt->setWordWrap(true);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("certificateStack"));
    m_stack->insertWidget(static_cast<int>(Page::Prompt), prompt);
    m_stack->insertWidget(static_cast<int>(Page::List), listPage);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_stack);

    connect(m_addButton, &QPushButton::clicked, this, &CertificateListWidget::addCertificate);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            &CertificateListWidget::editCertificate);
    // §4: one click in the Document column opens that row's document, with no
    // need to select the row first and no trip through the Attachment menu.
    // Every other column ignores a single click, which is what makes a single
    // click unambiguous here.
    connect(m_table, &QTableWidget::cellClicked, this,
            &CertificateListWidget::openAttachmentIfDocumentColumn);
    // The actions own the button clicks and the selection watching; all this
    // widget has to know is that something changed and the list is now stale.
    connect(m_ioActions, &CertificateIoActions::listChanged, this,
            &CertificateListWidget::reload);
    connect(m_actions, &CertificateListActions::listChanged, this,
            &CertificateListWidget::reload);
    // The filter works on computed severity, which is never stored, so it can
    // only be applied by recomputing and rebuilding the rows (§7).
    connect(m_needsAttentionCheck, &QCheckBox::toggled, this, &CertificateListWidget::reload);
    // §3: the selection is cleared before the reload, so Renew, Delete and
    // the attachment actions can never act on a row that has just been
    // hidden by the tab change.
    connect(m_categoryTabs, &CertificateCategoryTabs::selectionChanged, this, [this] {
        m_table->clearSelection();
        reload();
    });

    // In VESSEL mode the vessel is known once, at construction, and never
    // changes — the same certainty VesselDetailPage already relies on.
    if (installation.isConfigured() && installation.mode() == InstallationMode::Vessel) {
        setVesselId(installation.vesselId());
    } else {
        updateVisibleState();
    }
}

void CertificateListWidget::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
    m_rows->setVesselId(vesselId);
    m_actions->setVesselId(vesselId);
    m_ioActions->setVesselId(vesselId);
    reload();
}

QString CertificateListWidget::vesselId() const
{
    return m_vesselId;
}

void CertificateListWidget::setNeedsAttentionFilter(bool on)
{
    // §3 of the category-tabs spec: the banner's drill-down returns to All as
    // well as ticking the box, so nothing it is pointing at can be sitting
    // behind another tab.
    //
    // Without the tab bar's signal, because the checkbox below may reload
    // anyway and two reloads of the same list is work for nothing. The return
    // value says whether the tab actually moved, which is what decides
    // whether a reload is still needed when the checkbox does not change.
    const bool tabMoved = on && m_categoryTabs->selectAllWithoutSignal();

    if (m_needsAttentionCheck->isChecked() != on) {
        m_needsAttentionCheck->setChecked(on); // its own toggled() reloads
        return;
    }
    if (tabMoved) {
        reload();
    }
}

void CertificateListWidget::updateVisibleState()
{
    const Page page = m_vesselId.isEmpty() ? Page::Prompt : Page::List;
    m_stack->setCurrentIndex(static_cast<int>(page));
}

void CertificateListWidget::reload()
{
    updateVisibleState();

    if (m_vesselId.isEmpty()) {
        m_table->setRowCount(0);
        emit certificatesChanged();
        return;
    }

    // certificate-list-status-spec §8: this is the one legitimate place
    // "today" is read in this call chain. computeCertificateState() never
    // reads the clock itself — that is what makes it testable — so the screen
    // reads it once here and hands the same value down for every row.
    const QDate today = QDate::currentDate();

    // What goes in the rows is decided a layer down, in the service
    // (housekeeping-round-spec §3.2). This screen asks for the vessel's rows
    // and draws them.
    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    if (!m_rowSource.build(m_vesselId, m_categoryTabs->selected(),
                           m_needsAttentionCheck->isChecked(), today, &rows, &tallies)) {
        qWarning() << "Could not load certificates:" << m_rowSource.errorString();
        QMessageBox::warning(this, tr("Could not load certificates"),
                             m_rowSource.errorString());
        return;
    }

    // Sorting has to be off while rows are filled, or the table reorders
    // underneath the loop and cells land in the wrong rows.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(rows.size());

    for (int row = 0; row < rows.size(); ++row) {
        m_rows->fillRow(row, rows.at(row).certificate, rows.at(row).state);
    }

    m_table->setSortingEnabled(true);

    // After the rows, from the same pass that produced them.
    m_categoryTabs->updateLabels(tallies);

    // reload() can clear the selection, which would otherwise leave Renew
    // enabled with nothing selected. It is also what refreshes the extension
    // button's label after one has just been started or resolved.
    m_actions->updateButtonStates();

    emit certificatesChanged();
}
// §4: lives here rather than in CertificateListActions, because it is driven by
// *which cell was clicked* and not by *what is selected* — the same distinction
// that already keeps Add Certificate out of the actions class.
void CertificateListWidget::openAttachmentIfDocumentColumn(int row, int column)
{
    if (column != static_cast<int>(Column::Attachment)) {
        return;
    }

    QTableWidgetItem* numberItem = m_table->item(row, static_cast<int>(Column::ListNumber));
    if (numberItem == nullptr) {
        return;
    }
    const QString id = numberItem->data(CertificateRow::kCertificateIdRole).toString();

    const QString absolutePath = m_attachments.currentAbsolutePath(m_vesselId, id);
    if (absolutePath.isEmpty()) {
        // §4: an empty cell does nothing at all when clicked — not even a
        // message, because there is nothing to report.
        return;
    }

    // openUrl() handles a file and a folder identically, which is why nothing
    // above this line had to know which one is attached.
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))) {
        QMessageBox::warning(this, tr("Could not open the document"),
                             tr("Windows, macOS or your desktop refused to open:\n\n%1")
                                 .arg(QDir::toNativeSeparators(absolutePath)));
    }
}

void CertificateListWidget::addCertificate()
{
    if (m_vesselId.isEmpty()) {
        return;
    }

    // isRenewal spelled out: it sits before `parent`, so passing `this`
    // positionally here would silently convert the pointer to true.
    CertificateEditDialog dialog(m_repository, m_endorsements, m_attachments, m_vesselId,
                                 std::nullopt,
                                 /*isRenewal=*/false, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}

void CertificateListWidget::editCertificate(QTableWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }

    // The id lives on the first column, whichever cell was double-clicked.
    QTableWidgetItem* numberItem = m_table->item(item->row(), static_cast<int>(Column::ListNumber));
    if (numberItem == nullptr) {
        return;
    }
    const QString id = numberItem->data(CertificateRow::kCertificateIdRole).toString();

    std::optional<Certificate> certificate;
    if (!m_repository.findById(id, &certificate)) {
        QMessageBox::warning(this, tr("Could not open certificate"), m_repository.errorString());
        return;
    }
    if (!certificate.has_value()) {
        QMessageBox::information(this, tr("Certificate not found"),
                                 tr("This certificate is no longer in the database."));
        reload();
        return;
    }

    // Double-click keeps opening the ordinary, non-destructive edit — the
    // survey-clearing side effect belongs only to the Renew button.
    CertificateEditDialog dialog(m_repository, m_endorsements, m_attachments, m_vesselId,
                                 certificate,
                                 /*isRenewal=*/false, this);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
    }
}
