#include "modules/documents/ui/DocumentListWidget.h"

#include "core/DocumentsFolder.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentRepository.h"
#include "modules/documents/service/DocumentFileStore.h"
#include "modules/documents/ui/DocumentActions.h"
#include "modules/documents/ui/DocumentCategoryTabs.h"
#include "modules/documents/ui/DocumentEditDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum class Column {
    Category = 0,
    Reference = 1,
    Title = 2,
    Scope = 3,
    CurrentVersion = 4,
    RevisionDate = 5,
    Versions = 6
};
constexpr int kColumnCount = 7;

// The row data rides on the first cell, which is this column.
static_assert(static_cast<int>(Column::Category) == DocumentRow::kDataColumn,
              "row data must live on the column fillTable() writes it to");

QString categoryLabel(DocumentCategory category)
{
    switch (category) {
    case DocumentCategory::Sms:
        return DocumentListWidget::tr("SMS");
    case DocumentCategory::Circular:
        return DocumentListWidget::tr("Circular");
    case DocumentCategory::PlanManual:
        return DocumentListWidget::tr("Plan / manual");
    case DocumentCategory::Report:
        return DocumentListWidget::tr("Report");
    case DocumentCategory::Unset:
        break;
    }
    return QString();
}

} // namespace

DocumentListWidget::DocumentListWidget(DocumentRepository&        repository,
                                       DocumentFileStore&         files,
                                       VesselRepository&          vessels,
                                       const InstallationContext& installation,
                                       QWidget*                   parent)
    : QWidget(parent)
    , m_repository(repository)
    , m_files(files)
    , m_vessels(vessels)
    , m_installation(installation)
{
    m_addButton = new QPushButton(tr("&Add Document…"), this);
    m_addButton->setObjectName(QStringLiteral("addDocumentButton"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("documentSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("Search title or reference…"));
    m_searchEdit->setClearButtonEnabled(true);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("documentTable"));
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Category"), tr("Reference"), tr("Title"), tr("Scope"),
                                        tr("Current version"), tr("Revision date"),
                                        tr("Versions")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // edited via the dialog
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->sortByColumn(static_cast<int>(Column::Reference), Qt::AscendingOrder);

    // After the table, which both of them watch or write.
    m_tabs    = new DocumentCategoryTabs(this);
    m_actions = new DocumentActions(m_repository, m_files, m_table, this);

    auto* controlRow = new QHBoxLayout;
    controlRow->setContentsMargins(0, 0, 0, 0);
    controlRow->addWidget(m_addButton);
    controlRow->addWidget(m_actions->addVersionButton());
    controlRow->addWidget(m_actions->openButton());
    controlRow->addWidget(m_actions->versionsButton());
    controlRow->addWidget(m_actions->editButton());
    controlRow->addWidget(m_actions->deleteButton());
    controlRow->addStretch();
    controlRow->addWidget(new QLabel(tr("Search:"), this));
    controlRow->addWidget(m_searchEdit, 1);

    auto* hint = new QLabel(tr("Double-click a document to open its current version."), this);
    hint->setEnabled(false); // renders as secondary text

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controlRow);
    layout->addWidget(m_tabs->bar());
    layout->addWidget(m_table);
    layout->addWidget(hint);

    connect(m_addButton, &QPushButton::clicked, this, &DocumentListWidget::addDocument);
    connect(m_actions, &DocumentActions::listChanged, this, &DocumentListWidget::reload);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem* item) { m_actions->openDocument(item->row()); });

    // Both filters work on what is already loaded, so neither needs to go
    // back to the database — but both clear the selection first, so an action
    // can never act on a row that has just been hidden.
    connect(m_tabs, &DocumentCategoryTabs::selectionChanged, this, [this] {
        m_table->clearSelection();
        fillTable();
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] {
        m_table->clearSelection();
        fillTable();
    });

    reload();
}

void DocumentListWidget::setVesselId(const QString& vesselId)
{
    m_vesselId = vesselId;
    m_vesselName.clear();

    if (!m_vesselId.isEmpty()) {
        std::optional<Vessel> vessel;
        if (m_vessels.findById(m_vesselId, &vessel) && vessel.has_value()) {
            m_vesselName = vessel->name;
        }
    }

    reload();
}

QString DocumentListWidget::vesselId() const
{
    return m_vesselId;
}

void DocumentListWidget::reload()
{
    fillTable();
}

void DocumentListWidget::fillTable()
{
    QList<DocumentRepository::ListRow> rows;
    if (!m_repository.list(m_vesselId, &rows)) {
        qWarning() << "Could not load documents:" << m_repository.errorString();
        QMessageBox::warning(this, tr("Could not load documents"), m_repository.errorString());
        return;
    }

    const DocumentCategory tab    = m_tabs->selected();
    const QString          search = m_searchEdit->text().trimmed();

    // A vessel installation may not change fleet-wide documents (§4.3). The
    // rule is the repository's; this is the row carrying its answer so the
    // buttons can follow the selection without asking again.
    const bool onAVessel =
        m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;

    QHash<int, int> counts;
    QList<DocumentRepository::ListRow> visible;

    for (const DocumentRepository::ListRow& row : rows) {
        if (!search.isEmpty()
            && !row.document.title.contains(search, Qt::CaseInsensitive)
            && !row.document.reference.contains(search, Qt::CaseInsensitive)) {
            continue;
        }

        // Counted after the search and before the tab filter, so each tab
        // shows its own number whichever tab is open.
        counts[static_cast<int>(row.document.category)] += 1;
        counts[static_cast<int>(DocumentCategory::Unset)] += 1;

        if (tab != DocumentCategory::Unset && row.document.category != tab) {
            continue;
        }
        visible.append(row);
    }

    // Sorting off while the rows are filled, or the table reorders underneath
    // the loop and cells land in the wrong rows.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(visible.size());

    for (int row = 0; row < visible.size(); ++row) {
        const DocumentRepository::ListRow& entry    = visible.at(row);
        const Document&                    document = entry.document;

        auto* categoryItem = new QTableWidgetItem(categoryLabel(document.category));
        categoryItem->setData(DocumentRow::kDocumentIdRole, document.id);
        categoryItem->setData(DocumentRow::kEditableRole, !(onAVessel && document.isFleetWide()));

        m_table->setItem(row, static_cast<int>(Column::Category), categoryItem);
        m_table->setItem(row, static_cast<int>(Column::Reference),
                         new QTableWidgetItem(document.reference));
        m_table->setItem(row, static_cast<int>(Column::Title),
                         new QTableWidgetItem(document.title));
        m_table->setItem(row, static_cast<int>(Column::Scope),
                         new QTableWidgetItem(document.isFleetWide()
                                                  ? tr("Fleet")
                                                  : (m_vesselName.isEmpty() ? tr("This vessel")
                                                                            : m_vesselName)));

        const QString versionLabel =
            entry.currentVersion.has_value() ? entry.currentVersion->displayLabel() : QString();
        m_table->setItem(row, static_cast<int>(Column::CurrentVersion),
                         new QTableWidgetItem(versionLabel));

        const QDate revisionDate = entry.currentVersion.has_value()
                                       ? entry.currentVersion->revisionDate
                                       : QDate();
        m_table->setItem(row, static_cast<int>(Column::RevisionDate),
                         new QTableWidgetItem(revisionDate.isValid()
                                                  ? revisionDate.toString(
                                                        QStringLiteral("dd MMM yyyy"))
                                                  : QString()));

        // A number, not text, so the column sorts numerically.
        auto* versionsItem = new QTableWidgetItem;
        versionsItem->setData(Qt::DisplayRole, entry.versionCount);
        m_table->setItem(row, static_cast<int>(Column::Versions), versionsItem);
    }

    m_table->setSortingEnabled(true);
    m_tabs->updateCounts(counts);

    // The rebuild cleared the selection without a selection-changed signal,
    // so the buttons have to be told rather than waiting for one.
    m_actions->updateButtonStates();
}

void DocumentListWidget::addDocument()
{
    if (DocumentsFolder::path().isEmpty()) {
        QMessageBox::information(this, tr("No documents folder yet"),
                                 tr("Choose a documents folder in Settings first. That is "
                                    "where the files are kept."));
        return;
    }

    DocumentEditDialog dialog(m_installation, m_vesselId, m_vesselName, std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const Document document = dialog.document();

    // The file first, then the row: a row pointing at a file that was never
    // copied is a version nobody can open, while a copied file with no row is
    // a stray that harms nothing.
    DocumentVersion version = dialog.firstVersionValues();
    QString         error;
    if (!m_files.store(DocumentsFolder::path(), document, 1, dialog.firstVersionSourcePath(),
                       &version.filePath, &error)) {
        QMessageBox::warning(this, tr("Could not file the document"), error);
        return;
    }

    if (!m_repository.create(document, version, nullptr)) {
        QMessageBox::warning(this, tr("Could not save the document"),
                             m_repository.errorString());
        return;
    }

    reload();
}
