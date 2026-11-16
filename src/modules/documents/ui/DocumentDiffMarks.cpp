#include "modules/documents/ui/DocumentDiffMarks.h"

#include <QCoreApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextImageFormat>

namespace {

// The bar that stands beside a paragraph whose formatting changed. A block of
// a QTextDocument cannot be given a border, so the bar is a character — which
// is also what will print.
const QChar kMarginBar = QChar(0x258C); // ▌

constexpr int kBarIndent = 12;

QString translated(const char* text)
{
    return QCoreApplication::translate("DocumentDiffMarks", text);
}

// A real box: a bordered frame holding the picture and, under it, what
// happened to it. A frame is the only thing in a QTextDocument that draws a
// border, so the picture is lifted out and put back inside one.
void boxPicture(QTextDocument* document, const DocumentDiffMarks::Insertion& insertion)
{
    QTextCursor cursor(document);
    cursor.setPosition(insertion.position);

    QTextImageFormat imageFormat;
    if (insertion.what == DocumentDiffMarks::Insertion::What::PictureBox) {
        // The picture is in this document, so it keeps its own format: one
        // scaled to fit the page stays that size inside the box.
        cursor.setPosition(insertion.position + 1, QTextCursor::KeepAnchor);
        imageFormat = cursor.charFormat().toImageFormat();
        cursor.removeSelectedText();
    }
    if (imageFormat.name().isEmpty()) {
        // A removed picture is not in this document, but it is still in the
        // database — `document_image` is never changed — so it can be shown
        // by name. The width and height the old revision gave it are not
        // kept, which costs nothing for a picture being struck out anyway.
        imageFormat.setName(insertion.pictureName);
    }

    QTextFrameFormat frame;
    frame.setBorder(2);
    frame.setBorderBrush(insertion.colour);
    frame.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    frame.setPadding(4);
    frame.setMargin(2);

    QTextFrame* inserted = cursor.insertFrame(frame);
    if (inserted == nullptr) {
        return;
    }

    QTextCursor inner = inserted->firstCursorPosition();
    inner.insertImage(imageFormat);
    inner.insertBlock();
    inner.insertText(insertion.text, DocumentDiffMarks::noteFormat());
}

} // namespace

QColor DocumentDiffMarks::addedBackground()
{
    return QColor(198, 239, 206); // light green; black text still reads on it
}

QColor DocumentDiffMarks::removedForeground()
{
    return QColor(176, 0, 32); // red
}

QColor DocumentDiffMarks::formattingMark()
{
    // Blue, so a formatting change is not mistaken for a change of words.
    return QColor(27, 106, 201);
}

QTextCharFormat DocumentDiffMarks::addedFormat()
{
    QTextCharFormat format;
    format.setBackground(addedBackground());
    return format;
}

QTextCharFormat DocumentDiffMarks::removedFormat()
{
    QTextCharFormat format;
    format.setForeground(removedForeground());
    format.setFontStrikeOut(true);
    return format;
}

QTextCharFormat DocumentDiffMarks::noteFormat()
{
    QTextCharFormat format;
    format.setForeground(formattingMark());
    format.setFontItalic(true);
    return format;
}

QString DocumentDiffMarks::formattingNote()
{
    return translated("formatting changed");
}

QString DocumentDiffMarks::pictureLabel(const DocumentDiff::BlockChange& change)
{
    switch (change.kind) {
    case DocumentDiff::BlockChange::Kind::Added:
        return translated("picture added");
    case DocumentDiff::BlockChange::Kind::Removed:
        return translated("picture removed");
    case DocumentDiff::BlockChange::Kind::Changed:
        return translated("picture replaced");
    default:
        break;
    }
    return QString();
}

void DocumentDiffMarks::mergeFormat(QTextDocument* document, int position, int length,
                                    const QTextCharFormat& format)
{
    if (document == nullptr || length <= 0) {
        return;
    }
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.setPosition(position + length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
}

void DocumentDiffMarks::indentForBar(QTextDocument* document, int position)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);

    QTextBlockFormat format = cursor.blockFormat();
    format.setLeftMargin(format.leftMargin() + kBarIndent);
    cursor.setBlockFormat(format);
}

void DocumentDiffMarks::applyInsertion(QTextDocument* document, const Insertion& insertion)
{
    if (document == nullptr) {
        return;
    }

    switch (insertion.what) {
    case Insertion::What::PictureBox:
    case Insertion::What::RemovedPicture:
        boxPicture(document, insertion);
        return;

    case Insertion::What::RemovedWords: {
        QTextCursor cursor(document);
        cursor.setPosition(insertion.position);
        // Where they stood, struck through, left in place among the words
        // that stayed (§4.2).
        cursor.insertText(insertion.text, removedFormat());
        return;
    }

    case Insertion::What::RemovedBlock: {
        QTextCursor cursor(document);

        // A paragraph that is gone gets a paragraph of its own, so it reads
        // as what it was rather than running into the one that follows it.
        // A paragraph removed from the end of the page has no block to stand
        // before, so it is appended instead.
        if (insertion.position >= document->characterCount() - 1) {
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock();
            cursor.insertText(insertion.text, removedFormat());
            return;
        }

        cursor.setPosition(insertion.position);
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.insertText(insertion.text, removedFormat());
        cursor.insertBlock();
        return;
    }

    case Insertion::What::MarginBar: {
        QTextCursor cursor(document);
        cursor.setPosition(insertion.position);
        cursor.movePosition(QTextCursor::StartOfBlock);

        QTextCharFormat format;
        format.setForeground(formattingMark());
        cursor.insertText(QString(kMarginBar) + QLatin1Char(' '), format);
        return;
    }

    case Insertion::What::FormattingNote: {
        QTextCursor cursor(document);
        cursor.setPosition(insertion.position);
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText(QLatin1String("  ") + insertion.text, noteFormat());
        return;
    }
    }
}
