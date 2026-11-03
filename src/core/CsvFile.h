#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Reading and writing comma-separated files, as plain tables of text.
//
// Core rather than the certificates module (CLAUDE.md §5): a CSV file is not
// a certificate thing, and the modules this project still plans — spares,
// purchasing, crew — will all want tabular import and export of their own.
// Nothing here knows what a column means; that mapping belongs to whichever
// module owns the data.
//
// Qt ships no CSV parser, so this is hand-rolled. It is deliberately small
// and follows RFC 4180 exactly as certificate-import-export-spec §3.1 pins
// it down:
//
//  - comma delimiter;
//  - a field containing a comma, a double quote or a line break is wrapped in
//    double quotes, with "" standing for one embedded quote;
//  - written with CRLF line endings, because that is what Excel on Windows
//    produces and expects;
//  - written with a UTF-8 byte-order mark, which is not optional: without one
//    Excel on Windows reads UTF-8 as the local code page and mangles every
//    non-ASCII name in the file;
//  - read accepting CRLF or LF, and skipping a byte-order mark if there is
//    one rather than requiring it, so a file produced by hand in a plain text
//    editor still loads.
namespace CsvFile {

// The pure half, split out so the quoting rules are testable without
// touching the filesystem.
QString formatTable(const QList<QStringList>& rows);

// Returns false with a message in `error` only for something that cannot be
// read at all — an unterminated quoted field. Rows may have different field
// counts; lining them up against a header is the caller's business.
//
// A completely blank line produces no row, which is what makes a trailing
// newline at the end of a file harmless.
bool parseTable(const QString& text, QList<QStringList>* rows, QString* error);

// The filesystem half.
bool read(const QString& filePath, QList<QStringList>* rows, QString* error);
bool write(const QString& filePath, const QList<QStringList>& rows, QString* error);

} // namespace CsvFile
