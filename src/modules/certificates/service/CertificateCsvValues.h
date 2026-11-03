#pragma once

#include <QDate>
#include <QList>
#include <QString>

// Reading and writing one CSV cell's worth of value
// (certificate-import-export-spec §5.3, §5.4 and §6's list syntax).
//
// Split from CertificateCsvFormat, which owns the column *layout*: this file
// owns what a single value looks like in a file, and knows nothing about
// which column it came from. Keeping the two apart also keeps both inside the
// size guideline in CLAUDE.md §9.
//
// Every parse here is strict and refuses rather than guesses. That is the
// whole point: a cell this cannot read becomes a visible Invalid row, where a
// best-effort guess would become a wrong date nobody ever notices.
namespace CertificateCsvValues {

// Strictly YYYY-MM-DD. "02/03/2026" is the second of March to one reader and
// the third of February to another, so it is refused; so is "2026-2-4", and
// so is a date that does not exist.
bool   parseDate(const QString& value, QDate* date);
QString formatDate(const QDate& date);

// §5.4: true/false, yes/no, y/n or 1/0, in any case. Blank is false.
// Anything else is refused.
bool   parseBoolean(const QString& value, bool* result);
QString formatBoolean(bool value);

// §6's semicolon-separated date list. An empty entry — a trailing or doubled
// semicolon — is skipped rather than refused; a non-empty entry that is not a
// date fails, with the offending text in `badEntry` so the message can quote
// it.
bool   parseDateList(const QString& value, QList<QDate>* dates, QString* badEntry);
QString formatDateList(const QList<QDate>& dates);

} // namespace CertificateCsvValues
