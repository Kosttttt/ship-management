#pragma once

#include <QList>
#include <QString>

class QTextDocument;

// One piece of a page, in reading order: a paragraph, a heading, a list item,
// a table cell or a picture (document-history-spec.md §4.3 item 1).
//
// Plain data. Splitting a page into these is the first half of comparing two
// versions of it, and it is deliberately separate from the comparison itself:
// "what are the pieces of this page" and "what changed between these two
// lists of pieces" are two questions, and only the second one needs any
// cleverness.
struct DocumentBlock {
    enum class Kind {
        Paragraph,
        Heading,
        ListItem,
        TableCell,
        Picture,
    };

    Kind kind = Kind::Paragraph;

    // The words. Empty for a picture.
    QString text;

    // What the content calls the picture — the whole reference, e.g.
    // `doc-image:3f2a…`, kept as it is found. The domain does not need to
    // know how a picture is stored or what that scheme means; it only needs
    // to be able to say whether two pages name the same one.
    QString pictureName;

    // Everything about how it looks and nothing about what it says: heading
    // level, alignment, indent, list style, and the font and colour of each
    // run. Two blocks with the same text and different signatures are the
    // "formatting changed" case (§4.2).
    QString formatSignature;

    // Where this block sits in the document it was split from, as character
    // positions. The comparison does not use these; the view that paints the
    // marks does, which is why they are carried here rather than worked out
    // twice.
    int start  = 0;
    int length = 0;

    bool isPicture() const { return kind == Kind::Picture; }

    // What the sequence comparison matches on: the words, or the picture.
    //
    // **Not** the formatting. Two blocks with the same words and different
    // formatting must match each other, so the result can say "formatting
    // changed" instead of reporting a removal and an addition of the same
    // sentence (§4.3 item 4).
    QString matchKey() const;
};

namespace DocumentBlocks {

// The blocks of a page's stored content, in reading order.
//
// Qt's own rich-text document does the reading: hand-rolling an HTML parser
// to find paragraphs, lists and table cells would be far worse than using
// the one that already renders them, and QTextDocument is not a widget
// (CLAUDE.md §4 rule 1 permits QString and QDate on the same grounds).
QList<DocumentBlock> split(const QString& content);

// The same, for a document the caller already has. The view uses this to
// find the blocks in the very document it is about to paint, so a change
// reported as "block 7" lands on block 7 of what is on screen.
QList<DocumentBlock> splitDocument(const QTextDocument& document);

} // namespace DocumentBlocks
