#pragma once

#include "core/CsvFile.h"
#include "modules/certificates/service/CertificateCsvFormat.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QtTest>

// Building CSV files by hand for the import suites, shared by the two of them
// so neither owns it and neither grows past the size guideline in
// CLAUDE.md §9.
namespace CsvImportTestSupport {

// A full-width data row with only the named columns filled in. Addressing
// cells by column name rather than by position means a test says what it is
// setting, and adding a column to §3 never silently shifts a test's values
// into the wrong fields.
inline QStringList rowWith(const QHash<QString, QString>& values)
{
    const QStringList header = CertificateCsvFormat::headerRow();
    QStringList       cells(header.size());

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const int position = header.indexOf(it.key());
        if (position < 0) {
            qWarning() << "no such column:" << it.key();
            continue;
        }
        cells[position] = it.value();
    }
    return cells;
}

// A valid row: the three required columns, plus an expiry so a survey
// requirement is allowed to go with it.
inline QHash<QString, QString> validValues(const QString& name)
{
    return {
        {QStringLiteral("Name"), name},
        {QStringLiteral("Category"), QStringLiteral("STATUTORY")},
        {QStringLiteral("Issue Date"), QStringLiteral("2026-02-04")},
        {QStringLiteral("Expiry Date"), QStringLiteral("2031-02-04")},
    };
}

// Writes a file with §3's header and the given data rows, and returns the
// path. A header subset is used instead when `header` is given, for the tests
// about missing or absent columns.
inline QString writeCsv(const QString&            filePath,
                        const QList<QStringList>& dataRows,
                        const QStringList&        header = CertificateCsvFormat::headerRow())
{
    QList<QStringList> rows;
    rows.append(header);
    rows.append(dataRows);

    QString error;
    if (!CsvFile::write(filePath, rows, &error)) {
        qWarning() << "could not write the test file:" << error;
        return QString();
    }
    return filePath;
}

} // namespace CsvImportTestSupport
