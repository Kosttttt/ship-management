#include "modules/documents/domain/DocumentDiff.h"
#include "modules/documents/ui/DocumentDiffMarks.h"
#include "modules/documents/ui/DocumentDiffRenderer.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QtTest>

// The marks the comparison puts on the page (document-history-spec.md §4.2).
//
// No database: a comparison is painted onto a QTextDocument, and that is all
// this needs. What the marks *mean* is tested in tst_DocumentDiff; this is
// about whether they actually land on the page — which is a separate
// question, and the one the developer's screen answers.
class TestDocumentDiffRendering : public QObject
{
    Q_OBJECT

private slots:
    void anUnchangedPageIsNotMarked();
    void addedWordsGetTheAddedBackground();
    void removedWordsStayInPlaceStruckThrough();
    void aRemovedParagraphIsPutBackWhereItWas();
    void aParagraphRemovedFromTheEndIsAppended();
    void aFormattingChangeGetsABarAndANote();
    void aChangedPictureIsPutInABorderedBox();
    void theUntouchedTextOfThePageIsLeftExactlyAsItWas();
};

namespace {

// What the view does: load the new content, then mark it.
void render(QTextDocument* document, const QString& oldContent, const QString& newContent)
{
    document->setHtml(newContent);
    DocumentDiffRenderer::apply(document, DocumentDiff::compare(oldContent, newContent));
}

QString paragraphs(const QStringList& texts)
{
    QString html = QStringLiteral("<html><body>");
    for (const QString& text : texts) {
        html += QStringLiteral("<p>%1</p>").arg(text);
    }
    return html + QStringLiteral("</body></html>");
}

// The format of the first run whose text contains `needle`.
QTextCharFormat formatOf(const QTextDocument& document, const QString& needle)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.text().contains(needle)) {
                return fragment.charFormat();
            }
        }
    }
    return QTextCharFormat();
}

bool documentHasText(const QTextDocument& document, const QString& needle)
{
    return document.toPlainText().contains(needle);
}

// Every frame with a border, wherever it is — Qt's HTML import already puts
// the body in a frame of its own, so the box is a frame inside that one
// rather than a child of the root.
void collectBordered(QTextFrame* frame, QList<QTextFrame*>* found)
{
    for (QTextFrame* child : frame->childFrames()) {
        if (child->frameFormat().border() > 0) {
            found->append(child);
        }
        collectBordered(child, found);
    }
}

QList<QTextFrame*> borderedFrames(const QTextDocument& document)
{
    QList<QTextFrame*> found;
    collectBordered(document.rootFrame(), &found);
    return found;
}

} // namespace

void TestDocumentDiffRendering::anUnchangedPageIsNotMarked()
{
    const QString content = paragraphs({QStringLiteral("Muster at station 3.")});

    QTextDocument document;
    render(&document, content, content);

    QCOMPARE(document.toPlainText().trimmed(), QStringLiteral("Muster at station 3."));
    QVERIFY(!documentHasText(document, DocumentDiffMarks::formattingNote()));
    QCOMPARE(formatOf(document, QStringLiteral("Muster")).background(),
             QTextCharFormat().background());
}

void TestDocumentDiffRendering::addedWordsGetTheAddedBackground()
{
    QTextDocument document;
    render(&document, paragraphs({QStringLiteral("Fire drills are held monthly.")}),
           paragraphs({QStringLiteral("Fire drills are held quarterly.")}));

    QCOMPARE(formatOf(document, QStringLiteral("quarterly")).background().color(),
             DocumentDiffMarks::addedBackground());

    // And the words that did not change are not painted.
    QCOMPARE(formatOf(document, QStringLiteral("Fire drills")).background(),
             QTextCharFormat().background());
}

void TestDocumentDiffRendering::removedWordsStayInPlaceStruckThrough()
{
    QTextDocument document;
    render(&document, paragraphs({QStringLiteral("Fire drills are held monthly.")}),
           paragraphs({QStringLiteral("Fire drills are held quarterly.")}));

    // §4.2: left in place where it was, so the page reads "…held monthly
    // quarterly." with the old word struck out.
    const QString text = document.toPlainText();
    QVERIFY2(text.contains(QStringLiteral("monthly")), qPrintable(text));
    QVERIFY(text.indexOf(QStringLiteral("monthly"))
            < text.indexOf(QStringLiteral("quarterly")));

    const QTextCharFormat removed = formatOf(document, QStringLiteral("monthly"));
    QVERIFY(removed.fontStrikeOut());
    QCOMPARE(removed.foreground().color(), DocumentDiffMarks::removedForeground());
}

void TestDocumentDiffRendering::aRemovedParagraphIsPutBackWhereItWas()
{
    QTextDocument document;
    render(&document,
           paragraphs({QStringLiteral("Keep the deck clear."),
                       QStringLiteral("Report every near miss."), QStringLiteral("Sign the log.")}),
           paragraphs({QStringLiteral("Keep the deck clear."), QStringLiteral("Sign the log.")}));

    const QString text = document.toPlainText();
    QVERIFY2(text.contains(QStringLiteral("Report every near miss")), qPrintable(text));

    // Between the two paragraphs it stood between, not at the end.
    QVERIFY(text.indexOf(QStringLiteral("Keep the deck"))
            < text.indexOf(QStringLiteral("Report every")));
    QVERIFY(text.indexOf(QStringLiteral("Report every"))
            < text.indexOf(QStringLiteral("Sign the log")));

    // And as a paragraph of its own, struck through.
    QVERIFY(formatOf(document, QStringLiteral("Report every")).fontStrikeOut());
    QCOMPARE(document.blockCount(), 3);
}

void TestDocumentDiffRendering::aParagraphRemovedFromTheEndIsAppended()
{
    QTextDocument document;
    render(&document,
           paragraphs({QStringLiteral("Keep the deck clear."), QStringLiteral("Sign the log.")}),
           paragraphs({QStringLiteral("Keep the deck clear.")}));

    const QString text = document.toPlainText();
    QVERIFY2(text.contains(QStringLiteral("Sign the log")), qPrintable(text));
    // After the paragraph it used to follow, which is where it was.
    QVERIFY(text.indexOf(QStringLiteral("Keep the deck")) < text.indexOf(QStringLiteral("Sign the")));
    QVERIFY(formatOf(document, QStringLiteral("Sign the")).fontStrikeOut());
}

void TestDocumentDiffRendering::aFormattingChangeGetsABarAndANote()
{
    QTextDocument document;
    render(&document,
           QStringLiteral("<html><body><p>Do not enter alone.</p></body></html>"),
           QStringLiteral("<html><body><p><b>Do not enter alone.</b></p></body></html>"));

    // The note §4.2 asks for, said in words rather than only in a colour.
    QVERIFY2(documentHasText(document, DocumentDiffMarks::formattingNote()),
             qPrintable(document.toPlainText()));

    // The bar, and the room made for it beside the paragraph.
    QVERIFY(documentHasText(document, QString(QChar(0x258C))));

    QTextCursor cursor(&document);
    cursor.movePosition(QTextCursor::Start);
    QVERIFY2(cursor.blockFormat().leftMargin() > 0, "no room was made for the bar");

    // The words themselves are not marked as added or removed: nothing was
    // said differently.
    QCOMPARE(formatOf(document, QStringLiteral("Do not enter")).background(),
             QTextCharFormat().background());
    QVERIFY(!formatOf(document, QStringLiteral("Do not enter")).fontStrikeOut());
}

void TestDocumentDiffRendering::aChangedPictureIsPutInABorderedBox()
{
    const QString before = QStringLiteral(
        "<html><body><p>Muster list.</p><p><img src=\"doc-image:aaa\"></p></body></html>");
    const QString after = QStringLiteral(
        "<html><body><p>Muster list.</p><p><img src=\"doc-image:bbb\"></p></body></html>");

    QTextDocument document;
    document.setHtml(after);
    // The page itself draws no boxes, which is what makes the one below the
    // comparison's.
    QVERIFY(borderedFrames(document).isEmpty());

    DocumentDiffRenderer::apply(&document, DocumentDiff::compare(before, after));

    // A frame is the only thing in a QTextDocument that draws a border, so
    // the box is a bordered frame — and there is exactly one.
    const QList<QTextFrame*> boxes = borderedFrames(document);
    QCOMPARE(boxes.size(), 1);
    QCOMPARE(boxes.first()->frameFormat().borderStyle(), QTextFrameFormat::BorderStyle_Solid);

    // With the picture inside it, not beside it.
    QVERIFY2(boxes.first()->firstPosition() < boxes.first()->lastPosition(),
             "the box is empty");

    // Labelled, so a reader knows which of the three things happened.
    QVERIFY2(documentHasText(document, QStringLiteral("picture replaced")),
             qPrintable(document.toPlainText()));
}

void TestDocumentDiffRendering::theUntouchedTextOfThePageIsLeftExactlyAsItWas()
{
    // The marks are put on the page the reader is reading, so whatever did
    // not change has to come through the painting untouched — a comparison
    // that quietly rewrote the page would be worse than no comparison.
    const QString before = paragraphs({QStringLiteral("Chapter 1 scope."),
                                       QStringLiteral("Chapter 2 responsibilities."),
                                       QStringLiteral("Chapter 3 records.")});
    const QString after  = paragraphs({QStringLiteral("Chapter 1 scope."),
                                       QStringLiteral("Chapter 1A definitions."),
                                       QStringLiteral("Chapter 2 responsibilities."),
                                       QStringLiteral("Chapter 3 records.")});

    QTextDocument plain;
    plain.setHtml(after);

    QTextDocument marked;
    render(&marked, before, after);

    // Nothing was added to or taken from the words; only their colours
    // changed.
    QCOMPARE(marked.toPlainText(), plain.toPlainText());
    QCOMPARE(marked.blockCount(), plain.blockCount());

    // And the one new paragraph is the only thing painted.
    QCOMPARE(formatOf(marked, QStringLiteral("Chapter 1A")).background().color(),
             DocumentDiffMarks::addedBackground());
    QCOMPARE(formatOf(marked, QStringLiteral("Chapter 2")).background(),
             QTextCharFormat().background());
    QCOMPARE(formatOf(marked, QStringLiteral("Chapter 3")).background(),
             QTextCharFormat().background());
}

QTEST_MAIN(TestDocumentDiffRendering)
#include "tst_DocumentDiffRendering.moc"
