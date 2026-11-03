#include "modules/certificates/service/CertificateCsvFileChecks.h"

#include "core/Vessel.h"

#include <QSet>

namespace {

QString cellAt(const QStringList& cells, const QHash<QString, int>& columnIndex,
               const QString& column)
{
    const int position = columnIndex.value(column, -1);
    if (position < 0 || position >= cells.size()) {
        return QString();
    }
    return cells.at(position).trimmed();
}

} // namespace

bool CertificateCsvFileChecks::vesselMatches(const QList<QStringList>&  dataRows,
                                             const QHash<QString, int>& columnIndex,
                                             const Vessel&              vessel,
                                             QString*                   error)
{
    for (int index = 0; index < dataRows.size(); ++index) {
        const QString fileImo =
            cellAt(dataRows.at(index), columnIndex, QStringLiteral("Vessel IMO"));
        if (fileImo.isEmpty()) {
            continue;
        }
        if (fileImo == vessel.imoNumber) {
            continue;
        }

        // A file carrying another ship's IMO number almost always means the
        // wrong file was opened, so this refuses everything rather than
        // skipping a row.
        *error = QStringLiteral("Row %1 of the file is for IMO %2, but the selected vessel is "
                                "%3 (IMO %4).\n\n"
                                "Nothing has been imported. Check that this is the right file "
                                "for this ship.")
                     .arg(index + 1)
                     .arg(fileImo, vessel.name,
                          vessel.imoNumber.isEmpty() ? QStringLiteral("not set")
                                                     : vessel.imoNumber);
        return false;
    }
    return true;
}

bool CertificateCsvFileChecks::idsAreUnique(const QList<QStringList>&  dataRows,
                                            const QHash<QString, int>& columnIndex,
                                            QString*                   error)
{
    QSet<QString> seen;
    for (int index = 0; index < dataRows.size(); ++index) {
        const QString fileId = cellAt(dataRows.at(index), columnIndex, QStringLiteral("Id"));
        if (fileId.isEmpty()) {
            continue; // a blank Id is a new certificate, however many there are
        }
        if (seen.contains(fileId)) {
            *error = QStringLiteral(
                "The Id %1 appears on more than one row (row %2).\n\n"
                "Nothing has been imported: two rows claiming the same certificate cannot "
                "both be right.")
                         .arg(fileId)
                         .arg(index + 1);
            return false;
        }
        seen.insert(fileId);
    }
    return true;
}
