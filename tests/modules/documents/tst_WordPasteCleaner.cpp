#include "modules/documents/ui/WordPasteCleaner.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QtTest>

namespace {

// Roughly what Word puts on the clipboard: every run carrying a font, a size
// and a colour, a shaded paragraph, a list and a table with coloured cells.
// Trimmed of Word's conditional comments and `mso-` properties, which Qt's
// HTML reader ignores anyway.
QString wordHtml()
{
    return QStringLiteral(
        "<html><body lang=\"EN-GB\" style=\"font-family:Calibri; font-size:11.0pt\">"
        "<p style=\"background:#D9D9D9; color:#FF0000;"
        " font-family:'Times New Roman'; font-size:18.0pt\">"
        "<b style=\"font-family:Calibri\">Bold bit</b>"
        " <i>Italic bit</i>"
        " <u>Underlined bit</u>"
        " <span style=\"color:#00B050; font-family:Verdana; font-size:24.0pt\">Plain bit</span>"
        "</p>"
        "<ul>"
        "<li style=\"color:#FF0000; font-family:Verdana\">First item</li>"
        "<li>Second item</li>"
        "<li>Third item</li>"
        "</ul>"
        "<table border=\"1\">"
        "<tr>"
        "<td bgcolor=\"#FF0000\" style=\"color:#FFFF00\">Cell A</td>"
        "<td>Cell B</td>"
        "</tr>"
        "<tr><td>Cell C</td><td>Cell D</td></tr>"
        "</table>"
        "</body></html>");
}

// The format of the first run of text containing `needle`, which is how each
// assertion below asks "what happened to this bit of the paste".
QTextCharFormat formatOf(const QTextDocument& document, const QString& needle, bool* found)
{
    *found = false;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.text().contains(needle)) {
                *found = true;
                return fragment.charFormat();
            }
        }
    }
    return QTextCharFormat();
}

int blocksInAList(const QTextDocument& document)
{
    int count = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.textList() != nullptr) {
            ++count;
        }
    }
    return count;
}

QTextTable* firstTable(QTextDocument* document)
{
    for (QTextFrame* child : document->rootFrame()->childFrames()) {
        if (auto* table = qobject_cast<QTextTable*>(child)) {
            return table;
        }
    }
    return nullptr;
}

} // namespace

// document-management-spec.md §6.1 and §7: paste from Word.
//
// A manual is written in Word by four people over six years and then pasted
// in. Without this, it arrives as twelve different-looking documents with
// twelve fonts, and the thing a reader at sea notices first about a page is
// that it does not match the one before it.
//
// Structure in, decoration out: tested on HTML in and HTML out, with no
// window and no clipboard.
class TestWordPasteCleaner : public QObject
{
    Q_OBJECT

private slots:
    void boldItalicAndUnderlineSurvive();
    void onlyTheEmphasisedRunsAreEmphasised();
    void wordsFontsAndSizesAreDropped();
    void wordsColoursAreDropped();
    void paragraphShadingIsDropped();
    void listsSurvive();
    void tablesSurviveAndTheirShadingDoesNot();
    void aPictureKeepsItsReferenceAndItsSize();
    void emptyHtmlIsHandled();
};

void TestWordPasteCleaner::boldItalicAndUnderlineSurvive()
{
    QTextDocument document;
    document.setHtml(wordHtml());
    WordPasteCleaner::cleanInPlace(&document);

    bool                  found = false;
    const QTextCharFormat bold  = formatOf(document, QStringLiteral("Bold bit"), &found);
    QVERIFY(found);
    QCOMPARE(bold.fontWeight(), int(QFont::Bold));

    const QTextCharFormat italic = formatOf(document, QStringLiteral("Italic bit"), &found);
    QVERIFY(found);
    QVERIFY(italic.fontItalic());

    const QTextCharFormat underlined =
        formatOf(document, QStringLiteral("Underlined bit"), &found);
    QVERIFY(found);
    QVERIFY(underlined.fontUnderline());
}

void TestWordPasteCleaner::onlyTheEmphasisedRunsAreEmphasised()
{
    QTextDocument document;
    document.setHtml(wordHtml());
    WordPasteCleaner::cleanInPlace(&document);

    // The run-by-run difference is the part of the formatting that carries
    // meaning, so the plain run next to a bold one must still be plain. A
    // cleaner that flattened the whole paragraph would pass the test above
    // and lose this.
    bool                  found = false;
    const QTextCharFormat plain = formatOf(document, QStringLiteral("Plain bit"), &found);
    QVERIFY(found);
    QCOMPARE(plain.fontWeight(), int(QFont::Normal));
    QVERIFY(!plain.fontItalic());
    QVERIFY(!plain.fontUnderline());
}

void TestWordPasteCleaner::wordsFontsAndSizesAreDropped()
{
    QTextDocument document;
    document.setHtml(wordHtml());

    // Word's font really is in there before the clean, so the assertions
    // below are not passing for want of anything to drop.
    bool                  found  = false;
    const QTextCharFormat before = formatOf(document, QStringLiteral("Plain bit"), &found);
    QVERIFY(found);
    QCOMPARE(before.fontFamilies().toStringList().value(0), QStringLiteral("Verdana"));

    WordPasteCleaner::cleanInPlace(&document);

    const QTextCharFormat after = formatOf(document, QStringLiteral("Plain bit"), &found);
    QVERIFY(found);
    QVERIFY2(!after.hasProperty(QTextFormat::FontFamilies),
             "Word's font must not survive the paste");
    QVERIFY(!after.hasProperty(QTextFormat::FontPointSize));
    QVERIFY(!after.hasProperty(QTextFormat::FontSizeAdjustment));

    // And the same through the public entry point, as a string: the editor
    // gets HTML, not a document.
    const QString cleaned = WordPasteCleaner::clean(wordHtml());
    QVERIFY2(!cleaned.contains(QStringLiteral("Verdana")), qPrintable(cleaned.left(600)));
    QVERIFY2(!cleaned.contains(QStringLiteral("Calibri")), qPrintable(cleaned.left(600)));
    QVERIFY2(!cleaned.contains(QStringLiteral("Times New Roman")), qPrintable(cleaned.left(600)));
}

void TestWordPasteCleaner::wordsColoursAreDropped()
{
    QTextDocument document;
    document.setHtml(wordHtml());

    bool                  found  = false;
    const QTextCharFormat before = formatOf(document, QStringLiteral("Plain bit"), &found);
    QVERIFY(found);
    QVERIFY(before.hasProperty(QTextFormat::ForegroundBrush));

    WordPasteCleaner::cleanInPlace(&document);

    const QTextCharFormat after = formatOf(document, QStringLiteral("Plain bit"), &found);
    QVERIFY(found);
    QVERIFY2(!after.hasProperty(QTextFormat::ForegroundBrush),
             "green text pasted from Word must come out as the page's own colour");

    const QString cleaned = WordPasteCleaner::clean(wordHtml());
    QVERIFY2(!cleaned.contains(QStringLiteral("#00b050"), Qt::CaseInsensitive),
             qPrintable(cleaned.left(600)));
    QVERIFY2(!cleaned.contains(QStringLiteral("#ff0000"), Qt::CaseInsensitive),
             qPrintable(cleaned.left(600)));
}

void TestWordPasteCleaner::paragraphShadingIsDropped()
{
    QTextDocument document;
    document.setHtml(wordHtml());

    QTextBlock first = document.begin();
    QVERIFY(first.isValid());
    QVERIFY(first.blockFormat().hasProperty(QTextFormat::BackgroundBrush));

    WordPasteCleaner::cleanInPlace(&document);

    first = document.begin();
    QVERIFY2(!first.blockFormat().hasProperty(QTextFormat::BackgroundBrush),
             "Word's grey paragraph shading must not survive");
    QVERIFY2(!WordPasteCleaner::clean(wordHtml()).contains(QStringLiteral("#d9d9d9"),
                                                           Qt::CaseInsensitive),
             "the shading colour must be gone from the HTML too");
}

void TestWordPasteCleaner::listsSurvive()
{
    QTextDocument document;
    document.setHtml(wordHtml());
    QCOMPARE(blocksInAList(document), 3);

    WordPasteCleaner::cleanInPlace(&document);

    // A numbered procedure pasted from Word and arriving as three loose
    // paragraphs would have to be rebuilt by hand, which is most of the
    // work of retyping it.
    QCOMPARE(blocksInAList(document), 3);
    QVERIFY(document.toPlainText().contains(QStringLiteral("Second item")));
}

void TestWordPasteCleaner::tablesSurviveAndTheirShadingDoesNot()
{
    QTextDocument document;
    document.setHtml(wordHtml());

    QTextTable* before = firstTable(&document);
    QVERIFY(before != nullptr);
    QVERIFY(before->cellAt(0, 0).format().hasProperty(QTextFormat::BackgroundBrush));

    WordPasteCleaner::cleanInPlace(&document);

    QTextTable* table = firstTable(&document);
    QVERIFY2(table != nullptr, "a pasted table must still be a table");
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 2);

    // Cell shading is a format of its own, not reached by walking the
    // blocks — which is why it is checked separately rather than assumed.
    QVERIFY2(!table->cellAt(0, 0).format().hasProperty(QTextFormat::BackgroundBrush),
             "a red cell pasted from Word must come out unshaded");
    QVERIFY(!table->cellAt(0, 0).format().hasProperty(QTextFormat::ForegroundBrush));
}

void TestWordPasteCleaner::aPictureKeepsItsReferenceAndItsSize()
{
    // A picture is not emphasis, so the general rule would strip it to
    // nothing. Its reference is what makes it a picture at all.
    QTextDocument document;
    document.setHtml(QStringLiteral(
        "<p>Figure 1<br><img src=\"doc-image:abc-123\" width=\"320\" height=\"240\""
        " style=\"color:#FF0000\"></p>"));

    WordPasteCleaner::cleanInPlace(&document);

    bool             found  = false;
    QTextCharFormat  format = formatOf(document, QString(QChar::ObjectReplacementCharacter),
                                       &found);
    QVERIFY2(found, "the picture must still be in the document");
    QVERIFY(format.isImageFormat());

    const QTextImageFormat image = format.toImageFormat();
    QCOMPARE(image.name(), QStringLiteral("doc-image:abc-123"));
    QCOMPARE(image.width(), 320.0);
    QCOMPARE(image.height(), 240.0);
}

void TestWordPasteCleaner::emptyHtmlIsHandled()
{
    // Pasting nothing happens, and must not be a crash or an error.
    QVERIFY(!WordPasteCleaner::clean(QString()).isEmpty()); // an empty document is still HTML
    QVERIFY(WordPasteCleaner::clean(QString()).contains(QStringLiteral("<body")));
    WordPasteCleaner::cleanInPlace(nullptr);
}

QTEST_MAIN(TestWordPasteCleaner)
#include "tst_WordPasteCleaner.moc"
