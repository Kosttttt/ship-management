#include "modules/documents/ui/DocumentVersionsDialog.h"

#include "modules/documents/service/DocumentFileStore.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum class Column { Version = 0, Date = 1, Status = 2, Note = 3, File = 4 };
constexpr int kColumnCount = 5;

QString dateText(const QDate& date)
{
    // Blank rather than a placeholder: the date is optional information, and
    // an empty cell says "none recorded" more plainly than any word would.
    return date.isValid() ? date.toString(QStringLiteral("dd MMM yyyy")) : QString();
}

} // namespace

DocumentVersionsDialog::DocumentVersionsDialog(const QString&                documentTitle,
                                               const QList<DocumentVersion>& versions,
                                               const QString&                documentsRoot,
                                               QWidget*                      parent)
    : QDialog(parent)
    , m_versions(versions)
    , m_documentsRoot(documentsRoot)
{
    setWindowTitle(tr("Versions — %1").arg(documentTitle));
    setModal(true);
    setObjectName(QStringLiteral("documentVersionsDialog"));

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("documentVersionsTable"));
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Version"), tr("Date"), tr("Status"), tr("What changed"), tr("File")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setRowCount(m_versions.size());

    for (int row = 0; row < m_versions.size(); ++row) {
        const DocumentVersion& version = m_versions.at(row);

        m_table->setItem(row, static_cast<int>(Column::Version),
                         new QTableWidgetItem(version.displayLabel()));
        m_table->setItem(row, static_cast<int>(Column::Date),
                         new QTableWidgetItem(dateText(version.revisionDate)));
        // The list arrives newest first, so row 0 is the current version.
        m_table->setItem(row, static_cast<int>(Column::Status),
                         new QTableWidgetItem(row == 0 ? tr("Current") : tr("Superseded")));
        m_table->setItem(row, static_cast<int>(Column::Note),
                         new QTableWidgetItem(version.changeNote));
        m_table->setItem(row, static_cast<int>(Column::File),
                         new QTableWidgetItem(version.originalFileName));
    }

    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->resizeColumnsToContents();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* openButton =
        buttons->addButton(tr("&Open"), QDialogButtonBox::ActionRole);
    openButton->setObjectName(QStringLiteral("openVersionButton"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(openButton, &QPushButton::clicked, this, &DocumentVersionsDialog::openSelected);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { openSelected(); });

    if (!m_versions.isEmpty()) {
        m_table->selectRow(0);
    }
    resize(760, 360);
}

void DocumentVersionsDialog::openSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_versions.size()) {
        return;
    }

    const QString absolutePath =
        DocumentFileStore::absolutePathFor(m_documentsRoot, m_versions.at(row).filePath);
    if (absolutePath.isEmpty()) {
        QMessageBox::warning(this, tr("Could not open the file"),
                             tr("No documents folder is set, so this file cannot be found."));
        return;
    }

    // Opened in the computer's normal viewer (§1): this module stores files
    // and keeps their history, and leaves reading them to whatever already
    // reads that kind of file.
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))) {
        QMessageBox::warning(this, tr("Could not open the file"),
                             tr("Windows, macOS or your desktop refused to open:\n\n%1")
                                 .arg(QDir::toNativeSeparators(absolutePath)));
    }
}
