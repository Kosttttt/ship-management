#pragma once

#include <QString>

class QTextDocument;

// Paste from Word, cleaned (document-management-spec.md §6.1).
//
// **Keeps the structure** — paragraphs, bold, italic, underline, lists,
// tables, pictures — and **drops Word's fonts, sizes and colours**, so that
// every page of a manual looks the same however it was written. A manual
// pasted from twelve Word files by four people over six years is otherwise
// twelve different-looking manuals.
//
// Word headings may arrive as big bold text rather than Heading 1–3; the
// spec accepts that, and reapplying the heading style afterwards is a
// keystroke.
//
// A free function over HTML rather than something inside the editor, so it
// can be tested without a window: HTML in, HTML out.
namespace WordPasteCleaner {

// What goes into the editor instead of what was on the clipboard.
QString clean(const QString& html);

// The same work, on a document already loaded — what clean() is built from,
// exposed because the editor cleans its own document after a paste that came
// in some other way.
void cleanInPlace(QTextDocument* document);

} // namespace WordPasteCleaner
