#include "core/CsvFile.h"

#include <QFile>

namespace {

const QChar kQuote     = QLatin1Char('"');
const QChar kComma     = QLatin1Char(',');
const QChar kCr        = QLatin1Char('\r');
const QChar kLf        = QLatin1Char('\n');
const QChar kByteOrder = QChar(0xFEFF);

// The three bytes Excel looks for before it will treat a file as UTF-8.
const char kUtf8Bom[] = "\xEF\xBB\xBF";

QString formatField(const QString& field)
{
    const bool needsQuoting = field.contains(kComma) || field.contains(kQuote)
                              || field.contains(kCr) || field.contains(kLf);
    if (!needsQuoting) {
        return field;
    }

    QString escaped = field;
    escaped.replace(kQuote, QStringLiteral("\"\""));
    return kQuote + escaped + kQuote;
}

// A line that held nothing at all. Dropped rather than returned as a row of
// one empty field, so the newline every well-formed file ends with does not
// add a phantom final row.
bool isBlankRow(const QStringList& row)
{
    return row.size() == 1 && row.first().isEmpty();
}

} // namespace

QString CsvFile::formatTable(const QList<QStringList>& rows)
{
    QString text;
    for (const QStringList& row : rows) {
        QStringList fields;
        fields.reserve(row.size());
        for (const QString& field : row) {
            fields.append(formatField(field));
        }
        text += fields.join(kComma);
        text += QStringLiteral("\r\n");
    }
    return text;
}

bool CsvFile::parseTable(const QString& text, QList<QStringList>* rows, QString* error)
{
    rows->clear();

    QStringList row;
    QString     field;
    bool        inQuotes    = false;
    bool        fieldQuoted = false;
    int         line        = 1;

    // A byte-order mark survives the decode as one character; it is metadata
    // about the encoding, never part of the first column's name.
    int index = (!text.isEmpty() && text.at(0) == kByteOrder) ? 1 : 0;

    const auto endField = [&field, &fieldQuoted, &row]() {
        row.append(field);
        field.clear();
        fieldQuoted = false;
    };

    while (index < text.size()) {
        const QChar character = text.at(index);

        if (inQuotes) {
            if (character == kQuote) {
                // Doubled inside a quoted field means one literal quote.
                if (index + 1 < text.size() && text.at(index + 1) == kQuote) {
                    field += kQuote;
                    index += 2;
                    continue;
                }
                inQuotes = false;
                ++index;
                continue;
            }
            if (character == kLf) {
                ++line;
            }
            field += character;
            ++index;
            continue;
        }

        if (character == kQuote && field.isEmpty() && !fieldQuoted) {
            inQuotes    = true;
            fieldQuoted = true;
            ++index;
            continue;
        }
        if (character == kComma) {
            endField();
            ++index;
            continue;
        }
        if (character == kCr || character == kLf) {
            endField();
            if (!isBlankRow(row)) {
                rows->append(row);
            }
            row.clear();
            ++line;
            // CRLF is one line ending, not two.
            if (character == kCr && index + 1 < text.size() && text.at(index + 1) == kLf) {
                index += 2;
            } else {
                ++index;
            }
            continue;
        }

        field += character;
        ++index;
    }

    if (inQuotes) {
        *error = QStringLiteral("Line %1 of the file opens a quoted value that is never closed.")
                     .arg(line);
        return false;
    }

    // Whatever is left when the text runs out without a final line ending.
    if (!field.isEmpty() || !row.isEmpty()) {
        endField();
        if (!isBlankRow(row)) {
            rows->append(row);
        }
    }

    return true;
}

bool CsvFile::read(const QString& filePath, QList<QStringList>* rows, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not open %1:\n%2").arg(filePath, file.errorString());
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    return parseTable(QString::fromUtf8(bytes), rows, error);
}

bool CsvFile::write(const QString& filePath, const QList<QStringList>& rows, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("Could not write to %1:\n%2").arg(filePath, file.errorString());
        return false;
    }

    // Not QTextStream: the BOM and the CRLF endings have to be exactly these
    // bytes, and a text stream is free to translate line endings per platform.
    file.write(kUtf8Bom, 3);
    file.write(formatTable(rows).toUtf8());

    if (file.error() != QFileDevice::NoError) {
        *error = QStringLiteral("Could not write to %1:\n%2").arg(filePath, file.errorString());
        file.close();
        return false;
    }

    file.close();
    return true;
}
