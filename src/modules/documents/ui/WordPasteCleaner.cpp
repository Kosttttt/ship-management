#include "modules/documents/ui/WordPasteCleaner.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTextTableCell>

namespace {

// The three things worth keeping from a run of Word text, and nothing else.
//
// Built fresh rather than copied-and-cleared: QTextCharFormat carries a
// dozen properties, several of which Word sets and this program has no use
// for, and a list of things to clear would need adding to every time Word
// learned a new one. Starting from empty inverts that — only what is named
// here survives.
QTextCharFormat keepOnlyEmphasis(const QTextCharFormat& original)
{
    if (original.isImageFormat()) {
        // A picture is not emphasis. Its name is the reference to the image,
        // so losing that would lose the picture; its size is kept when Word
        // gave one, because a scaled-down screenshot pasted at full size is
        // not what the author saw.
        const QTextImageFormat source = original.toImageFormat();

        QTextImageFormat cleaned;
        cleaned.setName(source.name());
        if (source.width() > 0) {
            cleaned.setWidth(source.width());
        }
        if (source.height() > 0) {
            cleaned.setHeight(source.height());
        }
        return cleaned;
    }

    QTextCharFormat cleaned;
    // Word writes a numeric weight; anything at or above DemiBold reads as
    // bold to a person, and this program has only bold and not-bold.
    cleaned.setFontWeight(original.fontWeight() >= QFont::DemiBold ? QFont::Bold : QFont::Normal);
    cleaned.setFontItalic(original.fontItalic());
    cleaned.setFontUnderline(original.fontUnderline());
    return cleaned;
}

// What one run of text is, as a position in the document rather than as a
// QTextFragment: see collectRuns() below for why.
struct Run {
    int             position = 0;
    int             length   = 0;
    QTextCharFormat format;
};

// Every run of every block, read before anything is changed.
//
// **Collected first, applied afterwards.** Changing a character format
// merges and splits fragments, which invalidates both the block's fragment
// iterator and the fragment it is pointing at — and an invalidated fragment
// reports a position of whatever happens to be in that memory, which Qt
// then refuses with "position out of range" and the rest of the block goes
// uncleaned. Nothing here changes the text, only its formatting, so a
// position read now is still the same characters later.
QList<Run> collectRuns(const QTextDocument& document)
{
    QList<Run> runs;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        // Each run of same-formatted text in turn, rather than the whole
        // block at once, because a block's runs differ from one another and
        // that difference — this word bold, that one not — is exactly what
        // has to survive.
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.length() <= 0) {
                continue;
            }

            Run run;
            run.position = fragment.position();
            run.length   = fragment.length();
            run.format   = keepOnlyEmphasis(fragment.charFormat());
            runs.append(run);
        }
    }
    return runs;
}

// The blocks themselves: alignment, indent and list membership are structure
// and stay. Two things go.
//
// The **shading** — Word loves a grey paragraph background.
//
// And the **block's own character format**, which is a second place Word
// puts a font and a colour and is easy to miss: it is not part of any run,
// so cleaning the runs alone leaves it behind. It is what a `<p>` or an
// `<li>` carries as a whole, what an empty paragraph keeps, and what a list's
// bullet is drawn with — so a paste left with it has red bullets and Word's
// font on every blank line.
void cleanBlocks(QTextDocument* document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(document);
        cursor.setPosition(block.position());

        QTextBlockFormat blockFormat = block.blockFormat();
        blockFormat.clearBackground();
        cursor.setBlockFormat(blockFormat);

        cursor.setBlockCharFormat(keepOnlyEmphasis(block.charFormat()));
    }
}

} // namespace

void WordPasteCleaner::cleanInPlace(QTextDocument* document)
{
    if (document == nullptr) {
        return;
    }

    cleanBlocks(document);

    // The runs, then the formats — never both at once. See collectRuns().
    for (const Run& run : collectRuns(*document)) {
        QTextCursor cursor(document);
        cursor.setPosition(run.position);
        cursor.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(run.format);
    }

    // Table cell shading is a format of its own, so walking the blocks above
    // does not reach it. Tables are found through the document's root frame
    // rather than by scanning, which also covers a table inside a table.
    QList<QTextFrame*> frames{document->rootFrame()};
    for (int index = 0; index < frames.size(); ++index) {
        for (QTextFrame* child : frames.at(index)->childFrames()) {
            frames.append(child);
        }

        auto* table = qobject_cast<QTextTable*>(frames.at(index));
        if (table == nullptr) {
            continue;
        }
        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(row, column);
                if (!cell.isValid()) {
                    continue;
                }
                QTextCharFormat cellFormat = cell.format();
                cellFormat.clearBackground();
                cellFormat.clearForeground();
                cell.setFormat(cellFormat);
            }
        }
    }
}

QString WordPasteCleaner::clean(const QString& html)
{
    // A scratch document, so the editor's own content is never touched by a
    // paste that turns out to be unusable.
    QTextDocument scratch;
    scratch.setHtml(html);

    cleanInPlace(&scratch);

    // toHtml() writes the document's default font in the body style rather
    // than Word's, which is the point: every page then renders in this
    // program's font instead of whatever was installed on the machine the
    // text was written on.
    return scratch.toHtml();
}
