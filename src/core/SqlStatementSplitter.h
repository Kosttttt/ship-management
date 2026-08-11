#pragma once

#include <QString>
#include <QStringList>

// Breaks a .sql script into individual statements.
//
// QSqlQuery::exec() runs exactly one statement per call, so a migration
// containing a table and an index has to be split first. Semicolons inside
// quoted strings and inside comments are not statement boundaries, so the
// split cannot be a plain QString::split(';').
//
// Extracted from MigrationRunner so it can be tested directly and so that file
// stays under the size limit in CLAUDE.md §9.
namespace SqlStatementSplitter {

// Comments are stripped; empty statements are dropped.
// Known limitation: a compound BEGIN ... END block (a trigger) is split
// incorrectly. No migration needs one yet.
QStringList split(const QString& script);

} // namespace SqlStatementSplitter
