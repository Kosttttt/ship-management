#include "modules/certificates/ui/CertificateSelection.h"

#include "modules/certificates/ui/CertificateListRow.h"

#include <QItemSelectionModel>
#include <QTableWidget>

#include <algorithm>

namespace {

QString idOnRow(const QTableWidget* table, int row)
{
    QTableWidgetItem* item = table->item(row, CertificateRow::kDataColumn);
    return item == nullptr ? QString()
                           : item->data(CertificateRow::kCertificateIdRole).toString();
}

} // namespace

int CertificateSelection::singleRow(const QTableWidget* table)
{
    if (table == nullptr || table->selectionModel() == nullptr) {
        return -1;
    }

    const QModelIndexList selected = table->selectionModel()->selectedRows();

    // §2: exactly one, not "the first of however many". Two selected rows
    // means Renew has no single subject, so it must report none.
    return selected.size() == 1 ? selected.first().row() : -1;
}

QTableWidgetItem* CertificateSelection::singleDataItem(const QTableWidget* table)
{
    const int row = singleRow(table);
    if (row < 0) {
        return nullptr;
    }
    // const_cast: the caller reads the item's data, which QTableWidgetItem
    // only offers non-const, while asking the table about its selection does
    // not change it. Narrower than taking a non-const table everywhere.
    return const_cast<QTableWidget*>(table)->item(row, CertificateRow::kDataColumn);
}

QString CertificateSelection::singleCertificateId(const QTableWidget* table)
{
    const QTableWidgetItem* item = singleDataItem(table);
    return item == nullptr ? QString()
                           : item->data(CertificateRow::kCertificateIdRole).toString();
}

QDate CertificateSelection::singleExtensionExpiry(const QTableWidget* table)
{
    const QTableWidgetItem* item = singleDataItem(table);
    return item == nullptr ? QDate() : item->data(CertificateRow::kActiveExtensionRole).toDate();
}

QStringList CertificateSelection::allCertificateIds(const QTableWidget* table)
{
    QStringList ids;
    if (table == nullptr || table->selectionModel() == nullptr) {
        return ids;
    }

    // Sorted by row, so a confirmation lists the certificates in the order
    // they appear on screen rather than in whatever order they were clicked.
    QList<int> rows;
    const QModelIndexList selected = table->selectionModel()->selectedRows();
    rows.reserve(selected.size());
    for (const QModelIndex& index : selected) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());

    for (int row : rows) {
        const QString id = idOnRow(table, row);
        // An unreadable row contributes nothing rather than an empty id that
        // a later lookup would have to special-case.
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    return ids;
}
