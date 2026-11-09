#include "modules/certificates/ui/CertificateImportPreviewDialog.h"

#include "modules/certificates/service/CertificateIssueDatePlaceholder.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum class Column {
    RowNumber = 0,
    ListNumber = 1,
    Name       = 2,
    Outcome    = 3,
    Overwrite  = 4,
    Detail     = 5
};

constexpr int kColumnCount = 6;

QString outcomeLabel(ImportRowOutcome outcome)
{
    switch (outcome) {
    case ImportRowOutcome::New:
        return CertificateImportPreviewDialog::tr("New");
    case ImportRowOutcome::NewPreviouslyDeleted:
        return CertificateImportPreviewDialog::tr("New (previously deleted)");
    case ImportRowOutcome::Match:
        return CertificateImportPreviewDialog::tr("Match found");
    case ImportRowOutcome::Invalid:
        return CertificateImportPreviewDialog::tr("Invalid");
    }
    return QString();
}

// The Detail cell. An Invalid row says what is wrong with it; the others say
// what importing will do, so no row is left looking unexplained.
QString detailFor(const CertificateImportRow& row)
{
    switch (row.outcome) {
    case ImportRowOutcome::Invalid:
        // One line in a table cell: the validator's messages are written for
        // a message box and have a blank line in the middle.
        return row.reason.simplified();
    case ImportRowOutcome::New:
        return CertificateImportPreviewDialog::tr("Will be added.");
    case ImportRowOutcome::NewPreviouslyDeleted:
        return CertificateImportPreviewDialog::tr(
            "This certificate was deleted earlier, so it will be added again with a new "
            "identifier.");
    case ImportRowOutcome::Match:
        return CertificateImportPreviewDialog::tr(
            "Already on file. Tick Overwrite to replace it; survey history is never "
            "changed by an import.");
    }
    return QString();
}

} // namespace

CertificateImportPreviewDialog::CertificateImportPreviewDialog(
    const CertificateImportPreview& preview, QWidget* parent)
    : QDialog(parent)
    , m_preview(preview)
{
    setWindowTitle(tr("Import Certificates"));
    setModal(true);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("importPreviewSummary"));
    m_summary->setWordWrap(true);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("importPreviewTable"));
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Row"), tr("No."), tr("Name"), tr("Outcome"),
                                        tr("Overwrite"), tr("Detail")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // only the tick box is editable
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(static_cast<int>(Column::Detail),
                                                      QHeaderView::Stretch);

    // certificate-list-usability-fixes.md §3. Unticked by default, and that
    // default matters: a missing issue date is as often a data-entry slip
    // worth catching as it is a deliberate gap, so nothing is papered over
    // unless the user says so for this file.
    m_placeholderCheck = new QCheckBox(
        tr("Fill in a placeholder issue date (1990-01-01) for rows with none, and flag it "
           "in Notes"),
        this);
    m_placeholderCheck->setObjectName(QStringLiteral("importPlaceholderIssueDateCheck"));
    m_placeholderCheck->setChecked(false);
    m_placeholderCheck->setToolTip(
        tr("Only rows whose single problem is a missing issue date are affected. A row with "
           "an unreadable date, or any other problem, stays Invalid."));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_importButton = buttons->addButton(tr("&Import"), QDialogButtonBox::AcceptRole);
    m_importButton->setObjectName(QStringLiteral("importConfirmButton"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addWidget(m_table);
    layout->addWidget(m_placeholderCheck);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_placeholderCheck, &QCheckBox::toggled, this,
            &CertificateImportPreviewDialog::placeholderToggled);

    fillTable();
    resize(900, 500);
}

void CertificateImportPreviewDialog::fillTable()
{
    m_table->setRowCount(m_preview.rows.size());

    for (int row = 0; row < m_preview.rows.size(); ++row) {
        const CertificateImportRow& source = m_preview.rows.at(row);

        auto* rowNumber = new QTableWidgetItem(QString::number(source.rowNumber));
        m_table->setItem(row, static_cast<int>(Column::RowNumber), rowNumber);
        m_table->setItem(row, static_cast<int>(Column::ListNumber),
                         new QTableWidgetItem(source.parsed.certificate.listNumber));
        m_table->setItem(row, static_cast<int>(Column::Name),
                         new QTableWidgetItem(source.parsed.certificate.name));
        m_table->setItem(row, static_cast<int>(Column::Outcome),
                         new QTableWidgetItem(outcomeLabel(source.outcome)));

        // Only a matching row has a decision to make, so only a matching row
        // gets a tick box. Everything else gets an empty, untickable cell
        // rather than an unchecked box that looks like a choice.
        auto* overwrite = new QTableWidgetItem;
        if (source.outcome == ImportRowOutcome::Match) {
            overwrite->setFlags(overwrite->flags() | Qt::ItemIsUserCheckable);
            overwrite->setCheckState(source.overwrite ? Qt::Checked : Qt::Unchecked);
        } else {
            overwrite->setFlags(Qt::ItemIsEnabled);
        }
        m_table->setItem(row, static_cast<int>(Column::Overwrite), overwrite);

        m_table->setItem(row, static_cast<int>(Column::Detail),
                         new QTableWidgetItem(detailFor(source)));
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(static_cast<int>(Column::Detail),
                                                      QHeaderView::Stretch);

    const int invalid = m_preview.countOf(ImportRowOutcome::Invalid);
    const int added   = m_preview.countOf(ImportRowOutcome::New)
                      + m_preview.countOf(ImportRowOutcome::NewPreviouslyDeleted);
    const int matched = m_preview.countOf(ImportRowOutcome::Match);

    if (invalid > 0) {
        m_summary->setText(tr("%1 row(s) cannot be imported, so nothing can be imported from "
                              "this file yet. Correct the file and open it again.\n"
                              "Otherwise: %2 to add, %3 already on file.")
                               .arg(invalid)
                               .arg(added)
                               .arg(matched));
    } else {
        m_summary->setText(tr("%1 certificate(s) to add, %2 already on file. "
                              "Nothing is written until you choose Import.")
                               .arg(added)
                               .arg(matched));
    }

    // §5.5: blocked entirely while any row is Invalid.
    m_importButton->setEnabled(invalid == 0 && !m_preview.rows.isEmpty());
}

void CertificateImportPreviewDialog::applyTicks(CertificateImportPreview* target) const
{
    for (int row = 0; row < target->rows.size() && row < m_table->rowCount(); ++row) {
        if (target->rows[row].outcome != ImportRowOutcome::Match) {
            continue;
        }
        const QTableWidgetItem* item = m_table->item(row, static_cast<int>(Column::Overwrite));
        target->rows[row].overwrite = item != nullptr && item->checkState() == Qt::Checked;
    }
}

void CertificateImportPreviewDialog::placeholderToggled(bool on)
{
    // Read the ticks back first: fillTable() below rebuilds every cell, and a
    // match the user had already ticked must not quietly become a skip again.
    applyTicks(&m_preview);

    // §3: the rule is the service layer's. All this knows is that the answer
    // may have changed for some rows, and that the table has to be redrawn —
    // which also refreshes the summary line and the Import button.
    applyIssueDatePlaceholder(&m_preview, on);

    fillTable();
}

CertificateImportPreview CertificateImportPreviewDialog::decisions() const
{
    CertificateImportPreview resolved = m_preview;
    applyTicks(&resolved);
    return resolved;
}
