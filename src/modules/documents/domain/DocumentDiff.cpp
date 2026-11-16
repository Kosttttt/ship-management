#include "modules/documents/domain/DocumentDiff.h"

#include <QStringList>

namespace {

// Two blocks of at most this many words are paired whatever they say: a
// table cell, a heading or a short list item that now reads differently is a
// change to that cell, and there is too little text there for a word-by-word
// comparison to turn into confetti.
constexpr int kShortBlockWords = 4;

// The pairs of positions that two sequences have in common, in order, as
// long a run of them as there is — the ordinary dynamic-programming longest
// common subsequence.
//
// This is the whole reason a paragraph inserted in the middle of a page
// shows as one insertion: everything after it still matches, so nothing
// after it is reported. Comparing position by position would instead find
// every later paragraph "changed" (document-history-spec.md §4.3 item 2).
//
// A page is a few hundred blocks and a block a few hundred words, so a
// table of that size costs nothing and no outside library is needed.
QList<QPair<int, int>> commonPairs(const QStringList& older, const QStringList& newer)
{
    const int oldCount = older.size();
    const int newCount = newer.size();

    QList<QList<int>> lengths(oldCount + 1, QList<int>(newCount + 1, 0));
    for (int i = oldCount - 1; i >= 0; --i) {
        for (int j = newCount - 1; j >= 0; --j) {
            lengths[i][j] = (older.at(i) == newer.at(j))
                                ? lengths.at(i + 1).at(j + 1) + 1
                                : qMax(lengths.at(i + 1).at(j), lengths.at(i).at(j + 1));
        }
    }

    QList<QPair<int, int>> pairs;
    int                    i = 0;
    int                    j = 0;
    while (i < oldCount && j < newCount) {
        if (older.at(i) == newer.at(j)) {
            pairs.append({i, j});
            ++i;
            ++j;
        } else if (lengths.at(i + 1).at(j) >= lengths.at(i).at(j + 1)) {
            ++i;
        } else {
            ++j;
        }
    }
    return pairs;
}

// Words, each carrying the space that follows it, so that joining them back
// together reproduces the text exactly — which is what lets a view mark the
// pieces in place without rebuilding the paragraph.
QStringList splitWords(const QString& text)
{
    QStringList words;
    int         index = 0;

    while (index < text.size()) {
        const int start = index;
        while (index < text.size() && !text.at(index).isSpace()) {
            ++index;
        }
        while (index < text.size() && text.at(index).isSpace()) {
            ++index;
        }
        words.append(text.mid(start, index - start));
    }
    return words;
}

// Whether these two are the same block, edited — rather than one block
// removed and a different one put in its place.
//
// The spec is content for an unrelated replacement to read as a removal and
// an addition (§4.3), and that is the better answer: a word-by-word
// comparison of two paragraphs with nothing in common marks every word of
// both, which tells a reader less than showing them whole.
bool pairable(const DocumentBlock& was, const DocumentBlock& now)
{
    if (was.isPicture() != now.isPicture()) {
        return false; // a picture and a paragraph are not versions of each other
    }
    if (was.isPicture()) {
        return true; // a different picture in the same place: replaced
    }

    const QStringList olds = splitWords(was.text);
    const QStringList news = splitWords(now.text);
    if (olds.isEmpty() || news.isEmpty()) {
        return false;
    }

    const int longer = qMax(olds.size(), news.size());
    if (longer <= kShortBlockWords) {
        return true;
    }
    // A third of the longer one's words still in common.
    return commonPairs(olds, news).size() * 3 >= longer;
}

DocumentDiff::BlockChange removedChange(const DocumentBlock& was, int beforeNewIndex)
{
    DocumentDiff::BlockChange change;
    change.kind           = DocumentDiff::BlockChange::Kind::Removed;
    change.isPicture      = was.isPicture();
    change.beforeNewIndex = beforeNewIndex;
    change.removedText    = was.text;
    change.oldPictureName = was.pictureName;
    return change;
}

DocumentDiff::BlockChange addedChange(const DocumentBlock& now, int newIndex)
{
    DocumentDiff::BlockChange change;
    change.kind        = DocumentDiff::BlockChange::Kind::Added;
    change.isPicture   = now.isPicture();
    change.newIndex    = newIndex;
    change.pictureName = now.pictureName;
    return change;
}

// The blocks between two matches: what was there, and what is there now.
void appendGap(const QList<DocumentBlock>& oldBlocks, const QList<DocumentBlock>& newBlocks,
               int oldFrom, int oldTo, int newFrom, int newTo, DocumentDiff::Result* result)
{
    int o = oldFrom;
    int n = newFrom;

    while (o < oldTo && n < newTo) {
        const DocumentBlock& was = oldBlocks.at(o);
        const DocumentBlock& now = newBlocks.at(n);

        if (pairable(was, now)) {
            DocumentDiff::BlockChange change;
            change.kind           = DocumentDiff::BlockChange::Kind::Changed;
            change.isPicture      = now.isPicture();
            change.newIndex       = n;
            change.removedText    = was.text;
            change.pictureName    = now.pictureName;
            change.oldPictureName = was.pictureName;
            if (!now.isPicture()) {
                change.pieces = DocumentDiff::comparePieces(was.text, now.text);
            }
            result->changes.append(change);
        } else {
            // In place, in reading order: what it said, then what it says.
            result->changes.append(removedChange(was, n));
            result->changes.append(addedChange(now, n));
        }
        ++o;
        ++n;
    }

    while (o < oldTo) {
        result->changes.append(removedChange(oldBlocks.at(o), newTo));
        ++o;
    }
    while (n < newTo) {
        result->changes.append(addedChange(newBlocks.at(n), n));
        ++n;
    }
}

} // namespace

bool DocumentDiff::Result::isUnchanged() const
{
    for (const BlockChange& change : changes) {
        if (change.kind != BlockChange::Kind::Unchanged) {
            return false;
        }
    }
    return true;
}

int DocumentDiff::Result::countOf(BlockChange::Kind kind) const
{
    int count = 0;
    for (const BlockChange& change : changes) {
        if (change.kind == kind) {
            ++count;
        }
    }
    return count;
}

QList<DocumentDiff::Piece> DocumentDiff::comparePieces(const QString& oldText,
                                                       const QString& newText)
{
    const QStringList olds = splitWords(oldText);
    const QStringList news = splitWords(newText);

    QList<Piece> pieces;
    // Adjacent pieces with the same mark are joined, so three changed words
    // in a row are one green run rather than three.
    const auto push = [&pieces](const QString& text, Piece::Mark mark) {
        if (text.isEmpty()) {
            return;
        }
        if (!pieces.isEmpty() && pieces.last().mark == mark) {
            pieces.last().text += text;
            return;
        }
        pieces.append({text, mark});
    };

    int o = 0;
    int n = 0;
    for (const QPair<int, int>& pair : commonPairs(olds, news)) {
        while (o < pair.first) {
            push(olds.at(o++), Piece::Mark::Removed);
        }
        while (n < pair.second) {
            push(news.at(n++), Piece::Mark::Added);
        }
        push(news.at(pair.second), Piece::Mark::Kept);
        o = pair.first + 1;
        n = pair.second + 1;
    }
    while (o < olds.size()) {
        push(olds.at(o++), Piece::Mark::Removed);
    }
    while (n < news.size()) {
        push(news.at(n++), Piece::Mark::Added);
    }
    return pieces;
}

DocumentDiff::Result DocumentDiff::compareBlocks(const QList<DocumentBlock>& oldBlocks,
                                                 const QList<DocumentBlock>& newBlocks)
{
    QStringList oldKeys;
    QStringList newKeys;
    oldKeys.reserve(oldBlocks.size());
    newKeys.reserve(newBlocks.size());
    for (const DocumentBlock& block : oldBlocks) {
        oldKeys.append(block.matchKey());
    }
    for (const DocumentBlock& block : newBlocks) {
        newKeys.append(block.matchKey());
    }

    QList<QPair<int, int>> anchors = commonPairs(oldKeys, newKeys);
    // One past the end, so the blocks after the last match are dealt with by
    // the same code as every other gap.
    anchors.append({oldBlocks.size(), newBlocks.size()});

    Result result;
    int    o = 0;
    int    n = 0;

    for (const QPair<int, int>& anchor : anchors) {
        appendGap(oldBlocks, newBlocks, o, anchor.first, n, anchor.second, &result);

        if (anchor.second < newBlocks.size()) {
            // Matched, so the words are the same. All that can differ is how
            // it looks (§4.3 item 4).
            const DocumentBlock& was = oldBlocks.at(anchor.first);
            const DocumentBlock& now = newBlocks.at(anchor.second);

            BlockChange change;
            change.kind        = (was.formatSignature == now.formatSignature)
                                     ? BlockChange::Kind::Unchanged
                                     : BlockChange::Kind::FormattingOnly;
            change.isPicture   = now.isPicture();
            change.newIndex    = anchor.second;
            change.pictureName = now.pictureName;
            result.changes.append(change);
        }

        o = anchor.first + 1;
        n = anchor.second + 1;
    }
    return result;
}

DocumentDiff::Result DocumentDiff::compare(const QString& oldContent, const QString& newContent)
{
    return compareBlocks(DocumentBlocks::split(oldContent), DocumentBlocks::split(newContent));
}
