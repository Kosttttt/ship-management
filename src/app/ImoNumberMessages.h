#pragma once

#include "core/ImoNumberValidator.h"

#include <QString>

// Turns a validator result into words for the user.
//
// Core decides whether an IMO number is valid; this decides how to say so,
// which is what CLAUDE.md §4 rule 3 permits the UI layer to do. It lives in
// one place so the first-run wizard and the vessel edit form always report
// the same problem with the same words.
namespace ImoNumberMessages {

// Empty when the number is valid.
QString describe(ImoNumberValidator::Result result);

} // namespace ImoNumberMessages
