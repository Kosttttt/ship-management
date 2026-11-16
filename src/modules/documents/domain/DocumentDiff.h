#pragma once

#include "modules/documents/domain/DocumentBlocks.h"

#include <QList>
#include <QString>

// What changed between two versions of one page (document-history-spec.md
// §4.3).
//
// **A pure function of two contents.** No widgets, no database, no screen
// (CLAUDE.md §4 rule 1): it answers "what changed", and painting that answer
// is somebody else's job. That split is not tidiness — step 5 has to print
// the same changes as margin bars in a PDF, and a comparison that only knew
// how to colour a QTextEdit would have to be written twice.
//
// How it works, in the order the spec sets out:
//   1. split both versions into blocks in reading order (DocumentBlocks);
//   2. match the blocks with a longest-common-subsequence comparison on
//      their text, so a paragraph inserted in the middle is **one**
//      insertion and the paragraphs after it are untouched — the thing a
//      position-by-position comparison gets wrong, and what
//      tst_DocumentDiff proves;
//   3. for two blocks matched as a change, compare them again word by word,
//      so a one-word edit marks one word;
//   4. same words, different formatting → "formatting changed".
namespace DocumentDiff {

// A run of one block's text after the word-by-word comparison. Concatenating
// the pieces of a block reproduces its text exactly, which is what lets a
// view mark them in place.
struct Piece {
    enum class Mark {
        Kept,
        Added,
        Removed,
    };

    QString text;
    Mark    mark = Mark::Kept;
};

struct BlockChange {
    enum class Kind {
        Unchanged,
        Added,          // only the new version has it
        Removed,        // only the old version had it
        Changed,        // matched as a pair, and the words differ
        FormattingOnly, // matched, same words, looks different
    };

    Kind kind      = Kind::Unchanged;
    bool isPicture = false;

    // Which block of the **new** version this is, as an index into
    // DocumentBlocks::split(newContent). -1 for a removed block, which has no
    // place of its own in the new version.
    int newIndex = -1;

    // Where a removed block belongs: the new-version block it stood before.
    // Equal to the number of new blocks when it was removed from the end.
    int beforeNewIndex = -1;

    // What a removed block said, so a view can show it struck through
    // without going back to the old content.
    QString removedText;

    // The picture in the new version, and the one it replaced. Both set when
    // a picture was swapped for another.
    QString pictureName;
    QString oldPictureName;

    // Set for Kind::Changed. Empty otherwise — an added or removed block is
    // marked whole, and there is nothing to compare it against.
    QList<Piece> pieces;
};

struct Result {
    // Every block of the new version in reading order, with the removed
    // blocks of the old version interleaved where they stood. A view can
    // walk this list and mark as it goes.
    QList<BlockChange> changes;

    bool isUnchanged() const;
    int  countOf(BlockChange::Kind kind) const;
};

// The whole comparison, from two pages' stored content.
Result compare(const QString& oldContent, const QString& newContent);

// The same, from blocks already split — which is how the view compares what
// it has in front of it without parsing the content twice.
Result compareBlocks(const QList<DocumentBlock>& oldBlocks,
                     const QList<DocumentBlock>& newBlocks);

// The word-by-word comparison on its own. Public because it is worth testing
// by itself, and because a one-line "what changed in this sentence" is
// useful to a caller that already knows the block.
QList<Piece> comparePieces(const QString& oldText, const QString& newText);

} // namespace DocumentDiff
