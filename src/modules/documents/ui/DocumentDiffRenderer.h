#pragma once

#include "modules/documents/domain/DocumentDiff.h"

class QTextDocument;

// Painting a comparison onto the page it is about (document-history-spec.md
// §4.2): added text on green, removed text struck through in red and left
// where it was, a bar and a note against a paragraph whose formatting
// changed, and a box around a picture that was added, removed or replaced.
//
// **Inline, on the newer version**, rather than two panes side by side: it
// reads like the page itself, it works at any window width, and it is the
// shape step 5 has to print as change bars.
//
// Nothing here decides *what* changed — DocumentDiff did that, with no
// widgets and no database. This walks that answer and puts each mark where it
// belongs; what a mark looks like is DocumentDiffMarks.
namespace DocumentDiffRenderer {

// Marks `result` on `document`, which must already hold the **new** version's
// content — the one whose blocks the result's `newIndex` values count.
//
// Two passes, and the order is the whole trick: colours first, because
// changing a colour moves nothing, and then the insertions from the end of
// the document backwards, because inserting text shifts everything after it
// and would otherwise invalidate every position still to be used.
void apply(QTextDocument* document, const DocumentDiff::Result& result);

} // namespace DocumentDiffRenderer
