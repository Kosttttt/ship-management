#include "modules/documents/domain/DocumentBlocks.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

namespace {

// How a run of characters looks. QFont::toString() already carries family,
// size, weight, italic, underline and strikeout, so there is no list of
// properties here to forget to extend.
QString runSignature(const QTextCharFormat& format)
{
    return format.font().toString() + QLatin1Char('|') + format.foreground().color().name()
           + QLatin1Char('|') + format.background().color().name();
}

QString blockSignature(const QTextBlock& block)
{
    const QTextBlockFormat format = block.blockFormat();

    QString signature = QStringLiteral("h%1 a%2 i%3 m%4 b%5")
                            .arg(format.headingLevel())
                            .arg(static_cast<int>(format.alignment()))
                            .arg(format.indent())
                            .arg(format.leftMargin())
                            .arg(format.background().color().name());

    if (block.textList() != nullptr) {
        signature += QStringLiteral(" l%1")
                         .arg(static_cast<int>(block.textList()->format().style()));
    }

    // The block's own character format counts: it is what an empty paragraph
    // and a list's bullet are drawn with, and a page pasted from Word keeps
    // its font there as well as on the runs.
    signature += QStringLiteral(" [%1]").arg(runSignature(block.charFormat()));
    return signature;
}

DocumentBlock::Kind kindOf(const QTextBlock& block, bool insideTable)
{
    if (insideTable) {
        return DocumentBlock::Kind::TableCell;
    }
    if (block.textList() != nullptr) {
        return DocumentBlock::Kind::ListItem;
    }
    if (block.blockFormat().headingLevel() > 0) {
        return DocumentBlock::Kind::Heading;
    }
    return DocumentBlock::Kind::Paragraph;
}

// One block of the document can hold text and pictures together, so it can
// produce more than one of ours: the text around a picture is one piece and
// the picture is another. They come out in the order they are read in.
void appendBlock(const QTextBlock& block, bool insideTable, QList<DocumentBlock>* blocks)
{
    const DocumentBlock::Kind kind      = kindOf(block, insideTable);
    const QString             signature = blockSignature(block);

    DocumentBlock text;
    text.kind            = kind;
    text.formatSignature = signature;
    text.start           = -1;

    const auto flushText = [&]() {
        if (!text.text.isEmpty()) {
            blocks->append(text);
        }
        text.text  = QString();
        text.start = -1;
        text.length = 0;
    };

    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) {
            continue;
        }

        if (fragment.charFormat().isImageFormat()) {
            flushText();

            DocumentBlock picture;
            picture.kind        = DocumentBlock::Kind::Picture;
            picture.pictureName = fragment.charFormat().toImageFormat().name();
            picture.formatSignature =
                signature + QLatin1Char(' ') + runSignature(fragment.charFormat());
            picture.start  = fragment.position();
            picture.length = fragment.length();
            blocks->append(picture);
            continue;
        }

        if (text.start < 0) {
            text.start = fragment.position();
        }
        // A run's formatting belongs to the block's signature, so a word made
        // bold is a formatting change of that block rather than a different
        // block.
        text.formatSignature += QLatin1Char(' ') + runSignature(fragment.charFormat());
        text.text += fragment.text();
        text.length = (fragment.position() + fragment.length()) - text.start;
    }

    flushText();
}

void collectFrame(QTextFrame* frame, bool insideTable, QList<DocumentBlock>* blocks)
{
    if (frame == nullptr) {
        return;
    }

    // A table is a frame too, but its cells are not its children: reading
    // order through a table is row by row, and only the table itself knows
    // where its cells are.
    if (auto* table = qobject_cast<QTextTable*>(frame)) {
        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                if (!cell.isValid()) {
                    continue;
                }
                // A cell spanning several rows or columns is listed at its
                // own position only, so it is compared once.
                if (cell.row() != row || cell.column() != column) {
                    continue;
                }
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                    if (it.currentFrame() != nullptr) {
                        collectFrame(it.currentFrame(), true, blocks);
                    } else if (it.currentBlock().isValid()) {
                        appendBlock(it.currentBlock(), true, blocks);
                    }
                }
            }
        }
        return;
    }

    for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
        if (it.currentFrame() != nullptr) {
            collectFrame(it.currentFrame(), insideTable, blocks);
        } else if (it.currentBlock().isValid()) {
            appendBlock(it.currentBlock(), insideTable, blocks);
        }
    }
}

} // namespace

QString DocumentBlock::matchKey() const
{
    if (isPicture()) {
        return QStringLiteral("picture:") + pictureName;
    }
    // Leading and trailing space is not a difference anybody means, and the
    // editor adds and removes it without being asked.
    return QStringLiteral("text:") + text.simplified();
}

QList<DocumentBlock> DocumentBlocks::splitDocument(const QTextDocument& document)
{
    QList<DocumentBlock> blocks;
    collectFrame(document.rootFrame(), false, &blocks);
    return blocks;
}

QList<DocumentBlock> DocumentBlocks::split(const QString& content)
{
    QTextDocument document;
    // Pictures are not loaded: this is about what the page says, and a
    // picture is identified by its reference. Comparing the bytes of two
    // pictures would answer a question nobody asked.
    document.setHtml(content);
    return splitDocument(document);
}
