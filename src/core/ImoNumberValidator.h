#pragma once

#include <QString>

// Validates an IMO number by its check digit (first-run-wizard-spec §3).
//
// A pure function: no widgets, no SQL, no I/O, so it obeys the domain rule in
// CLAUDE.md §4.1 and is unit-testable without a window or a database. It lives
// in core/ because both the wizard (core) and the later Vessel CRUD form
// (module-adjacent) need it without depending on each other.
namespace ImoNumberValidator {

enum class Result {
    Valid,
    Empty,              // nothing entered
    WrongLength,        // not exactly seven digits
    CheckDigitMismatch  // seven digits, but the last one does not agree
};

// Removes every non-digit character, so "IMO 9074729" and "9074729" are the
// same input. People are used to writing the prefix; making them delete it
// would be a pointless obstacle.
QString digitsOnly(const QString& input);

// Applies digitsOnly() first, then checks the result.
Result validate(const QString& input);

// Convenience for the common case.
bool isValid(const QString& input);

} // namespace ImoNumberValidator
