#include "modules/documents/ui/DocumentDiffRenderer.h"

#include "modules/documents/ui/DocumentDiffMarks.h"

#include <QTextDocument>

#include <algorithm>

namespace {

using Kind      = DocumentDiff::BlockChange::Kind;
using Mark      = DocumentDiff::Piece::Mark;
using Insertion = DocumentDiffMarks::Insertion;
using What      = DocumentDiffMarks::Insertion::What;

} // namespace

void DocumentDiffRenderer::apply(QTextDocument* document, const DocumentDiff::Result& result)
{
    if (document == nullptr) {
        return;
    }

    // The new version's blocks, found in **this** document, so a change
    // reported as "block 7" lands on block 7 of what is about to be shown
    // rather than on block 7 of a document parsed somewhere else.
    const QList<DocumentBlock> blocks = DocumentBlocks::splitDocument(*document);

    const auto blockAt = [&blocks](int index) {
        return (index >= 0 && index < blocks.size()) ? blocks.at(index) : DocumentBlock();
    };
    const auto documentEnd = [document]() { return document->characterCount() - 1; };

    QList<Insertion> insertions;

    // Pass one: colours. Nothing here changes a length, so every position
    // stays valid for the whole pass.
    for (const DocumentDiff::BlockChange& change : result.changes) {
        switch (change.kind) {
        case Kind::Unchanged:
            break;

        case Kind::Added: {
            const DocumentBlock block = blockAt(change.newIndex);
            if (change.isPicture) {
                insertions.append({block.start, What::PictureBox,
                                   DocumentDiffMarks::pictureLabel(change), change.pictureName,
                                   DocumentDiffMarks::addedBackground().darker(150)});
            } else {
                DocumentDiffMarks::mergeFormat(document, block.start, block.length,
                                               DocumentDiffMarks::addedFormat());
            }
            break;
        }

        case Kind::Removed: {
            // Where it stood: the start of the block that now follows it, or
            // the end of the page when it was the last thing there.
            const DocumentBlock before   = blockAt(change.beforeNewIndex);
            const bool          atTheEnd = change.beforeNewIndex >= blocks.size();
            const int           position = atTheEnd ? documentEnd() : before.start;

            insertions.append({position,
                               change.isPicture ? What::RemovedPicture : What::RemovedBlock,
                               change.isPicture ? DocumentDiffMarks::pictureLabel(change)
                                                : change.removedText,
                               change.oldPictureName, DocumentDiffMarks::removedForeground()});
            break;
        }

        case Kind::Changed: {
            const DocumentBlock block = blockAt(change.newIndex);
            if (change.isPicture) {
                insertions.append({block.start, What::PictureBox,
                                   DocumentDiffMarks::pictureLabel(change), change.pictureName,
                                   DocumentDiffMarks::formattingMark()});
                break;
            }

            // Word by word. Only the kept and added pieces are in this
            // document, so they are what the position walks through; a
            // removed piece is inserted where it was and advances nothing.
            int position = block.start;
            for (const DocumentDiff::Piece& piece : change.pieces) {
                if (piece.mark == Mark::Removed) {
                    insertions.append({position, What::RemovedWords, piece.text, QString(),
                                       DocumentDiffMarks::removedForeground()});
                    continue;
                }
                if (piece.mark == Mark::Added) {
                    DocumentDiffMarks::mergeFormat(document, position, piece.text.size(),
                                                   DocumentDiffMarks::addedFormat());
                }
                position += piece.text.size();
            }
            break;
        }

        case Kind::FormattingOnly: {
            const DocumentBlock block = blockAt(change.newIndex);
            DocumentDiffMarks::indentForBar(document, block.start);
            insertions.append({block.start, What::MarginBar, QString(), QString(),
                               DocumentDiffMarks::formattingMark()});
            insertions.append({block.start, What::FormattingNote,
                               DocumentDiffMarks::formattingNote(), QString(),
                               DocumentDiffMarks::formattingMark()});
            break;
        }
        }
    }

    // Pass two: from the back of the page forwards.
    //
    // A note goes at the end of its block and a bar at the start, so the two
    // are listed at the same position; the note has to go in first, because
    // once the bar is there the end of that block has moved.
    std::stable_sort(insertions.begin(), insertions.end(),
                     [](const Insertion& a, const Insertion& b) {
                         if (a.position != b.position) {
                             return a.position > b.position;
                         }
                         return a.what == What::FormattingNote && b.what != What::FormattingNote;
                     });

    for (const Insertion& insertion : insertions) {
        DocumentDiffMarks::applyInsertion(document, insertion);
    }
}
