#include "modules/documents/ui/DocumentHistoryDialog.h"

#include "modules/documents/service/DocumentRestorer.h"
#include "modules/documents/ui/DocumentRevisionDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString formatDate(const QDateTime& utc)
{
    // Stored UTC, shown local (CLAUDE.md §6.2).
    return utc.toLocalTime().toString(QStringLiteral("dd MMM yyyy"));
}

} // namespace

DocumentHistoryDialog::DocumentHistoryDialog(DocumentHistoryReader& history,
                                             DocumentRestorer&      restorer,
                                             DocumentImageStore& images, QWidget* parent)
    : QDialog(parent)
    , m_history(history)
    , m_restorer(restorer)
    , m_images(images)
{
    setObjectName(QStringLiteral("documentHistoryDialog"));
    setModal(true);
    resize(720, 420);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("pageHistoryTable"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Two rows at once, so Compare… can mean "these two" without a second
    // dialog in the ordinary case (§2).
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);

    m_viewButton     = new QPushButton(tr("&View"), this);
    m_previousButton = new QPushButton(tr("Compare with &previous"), this);
    m_compareButton  = new QPushButton(tr("&Compare…"), this);
    m_restoreButton  = new QPushButton(tr("&Restore…"), this);
    m_viewButton->setObjectName(QStringLiteral("viewRevisionButton"));
    m_previousButton->setObjectName(QStringLiteral("compareWithPreviousButton"));
    m_compareButton->setObjectName(QStringLiteral("compareRevisionsButton"));
    m_restoreButton->setObjectName(QStringLiteral("restoreRevisionButton"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(m_viewButton);
    buttonRow->addWidget(m_previousButton);
    buttonRow->addWidget(m_compareButton);
    buttonRow->addWidget(m_restoreButton);
    buttonRow->addStretch();

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttonRow);
    layout->addWidget(closeBox);

    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &DocumentHistoryDialog::updateButtons);
    connect(m_table, &QTableWidget::doubleClicked, this, &DocumentHistoryDialog::viewSelected);
    connect(m_viewButton, &QPushButton::clicked, this, &DocumentHistoryDialog::viewSelected);
    connect(m_previousButton, &QPushButton::clicked, this,
            &DocumentHistoryDialog::compareWithPrevious);
    connect(m_compareButton, &QPushButton::clicked, this,
            &DocumentHistoryDialog::compareWithAnother);
    connect(m_restoreButton, &QPushButton::clicked, this,
            &DocumentHistoryDialog::restoreSelected);
}

bool DocumentHistoryDialog::showFor(const DocumentPage& page, const QString& pageTitle,
                                    bool manualSpace, bool editable)
{
    m_page        = page;
    m_pageTitle   = pageTitle;
    m_manualSpace = manualSpace;
    m_editable    = editable;

    setWindowTitle(tr("History — %1").arg(pageTitle));

    if (!m_history.listPageHistory(page.id, &m_entries)) {
        m_errorString = m_history.errorString();
        return false;
    }

    fillTable();
    updateButtons();
    return true;
}

void DocumentHistoryDialog::fillTable()
{
    QStringList headers{tr("Rev")};
    if (m_manualSpace) {
        headers.append(tr("Manual rev"));
    }
    headers << tr("Date") << tr("By") << tr("Note");

    m_table->clear();
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setRowCount(m_entries.size());

    for (int row = 0; row < m_entries.size(); ++row) {
        const DocumentPageRevision& revision = m_entries.at(row).revision;

        int column = 0;
        m_table->setItem(row, column++,
                         new QTableWidgetItem(QString::number(revision.number)));
        if (m_manualSpace) {
            const int manual = m_entries.at(row).manualNumber;
            m_table->setItem(row, column++,
                             new QTableWidgetItem(manual > 0 ? tr("Rev %1").arg(manual)
                                                             : QString()));
        }
        m_table->setItem(row, column++,
                         new QTableWidgetItem(formatDate(revision.publishedAtUtc)));
        m_table->setItem(row, column++, new QTableWidgetItem(revision.createdBy));
        m_table->setItem(row, column++,
                         new QTableWidgetItem(m_entries.at(row).shownNote()));
    }

    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->resizeColumnsToContents();
    if (!m_entries.isEmpty()) {
        m_table->selectRow(0); // the revision in force, which is what a reader wants first
    }
}

QList<int> DocumentHistoryDialog::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end()); // newest first, as the table is ordered
    return rows;
}

void DocumentHistoryDialog::updateButtons()
{
    const QList<int> rows = selectedRows();

    m_viewButton->setEnabled(rows.size() == 1);
    m_compareButton->setEnabled(rows.size() == 1 || rows.size() == 2);

    // Nothing before the page's first revision, so there is nothing to
    // compare it with.
    const bool hasPrevious = rows.size() == 1 && rows.first() + 1 < m_entries.size();
    m_previousButton->setEnabled(hasPrevious);

    m_restoreButton->setEnabled(m_editable && rows.size() == 1);
    m_restoreButton->setToolTip(
        m_editable ? QString()
                   : tr("This space is managed at the office and is read-only here."));
}

QString DocumentHistoryDialog::describe(
    const DocumentHistoryReader::PageHistoryEntry& entry) const
{
    // "Rev 8" in a manual, "Rev 3" of the page in a collection — the number
    // the reader of that space recognises.
    const int number = m_manualSpace && entry.manualNumber > 0 ? entry.manualNumber
                                                               : entry.revision.number;
    return tr("Rev %1 (%2)").arg(number).arg(formatDate(entry.revision.publishedAtUtc));
}

void DocumentHistoryDialog::viewSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.size() != 1) {
        return;
    }

    const DocumentHistoryReader::PageHistoryEntry& entry = m_entries.at(rows.first());
    DocumentRevisionDialog* dialog = DocumentRevisionDialog::view(
        tr("%1 — %2").arg(entry.revision.title, describe(entry)), entry.revision.title,
        entry.revision.content, m_images, this);
    dialog->exec();
    dialog->deleteLater();
}

void DocumentHistoryDialog::compareRevisions(const DocumentPageRevision& older,
                                             const DocumentPageRevision& newer)
{
    // The legend's wording lives in the dialog that shows it, so the Record
    // of Revisions says it the same way.
    DocumentRevisionDialog* dialog =
        DocumentRevisionDialog::compareRevisions(older, newer, m_images, this);
    dialog->exec();
    dialog->deleteLater();
}

void DocumentHistoryDialog::compareWithPrevious()
{
    const QList<int> rows = selectedRows();
    if (rows.size() != 1 || rows.first() + 1 >= m_entries.size()) {
        return;
    }
    // Newest first, so the one after it in the list is the one before it in
    // time.
    compareRevisions(m_entries.at(rows.first() + 1).revision,
                     m_entries.at(rows.first()).revision);
}

void DocumentHistoryDialog::compareWithAnother()
{
    QList<int> rows = selectedRows();

    if (rows.size() == 1) {
        // One picked, so ask for the other (§2: "Compare… (pick any two)").
        QStringList labels;
        QList<int>  choices;
        for (int row = 0; row < m_entries.size(); ++row) {
            if (row == rows.first()) {
                continue;
            }
            labels.append(describe(m_entries.at(row)));
            choices.append(row);
        }
        if (labels.isEmpty()) {
            return;
        }

        bool          accepted = false;
        const QString chosen =
            QInputDialog::getItem(this, tr("Compare"), tr("With which revision?"), labels, 0,
                                  false, &accepted);
        if (!accepted) {
            return;
        }
        const int index = labels.indexOf(chosen);
        if (index < 0) {
            return;
        }
        rows.append(choices.at(index));
        std::sort(rows.begin(), rows.end());
    }

    if (rows.size() != 2) {
        return;
    }
    // The table is newest first, so the later row is the older revision.
    compareRevisions(m_entries.at(rows.at(1)).revision, m_entries.at(rows.at(0)).revision);
}

void DocumentHistoryDialog::restoreSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.size() != 1 || !m_editable) {
        return;
    }

    const DocumentHistoryReader::PageHistoryEntry& entry = m_entries.at(rows.first());

    // §5: the draft is replaced, so ask when there is one to lose.
    if (m_page.hasDraft()) {
        QMessageBox confirm(this);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(tr("Replace the draft?"));
        confirm.setText(tr("This replaces your unpublished changes to %1.").arg(m_pageTitle));
        confirm.setInformativeText(
            tr("%1 becomes the page's draft. Nothing is published until you publish it.")
                .arg(describe(entry)));
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) {
            return;
        }
    }

    if (!m_restorer.restoreIntoDraft(entry.revision.id)) {
        QMessageBox::warning(this, tr("Could not restore the revision"),
                             m_restorer.errorString());
        return;
    }

    m_restored = true;
    // Closed rather than left open: the page now has an unpublished change,
    // and that is where the author's attention belongs.
    accept();
}
