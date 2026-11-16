#include "modules/documents/domain/DocumentDiff.h"

#include <QtTest>

using Kind = DocumentDiff::BlockChange::Kind;
using Mark = DocumentDiff::Piece::Mark;

// The comparison rules (document-history-spec.md §4.3, §7).
//
// **No database and no widgets here at all** — not a temporary database, not
// an in-memory one. The comparison is a pure function of two pieces of
// content, which is what makes it testable like this and reusable by step
// 5's printed change bars (CLAUDE.md §4 rule 1).
class TestDocumentDiff : public QObject
{
    Q_OBJECT

private slots:
    void theSameContentHasNoChanges();
    void oneChangedWordMarksOnlyThatWord();
    void aParagraphInsertedInTheMiddleIsOneInsertion();
    void aNaivePositionByPositionComparisonGetsThatWrong();
    void aParagraphRemovedFromTheMiddleIsOneRemoval();
    void anUnrelatedParagraphReadsAsARemovalAndAnAddition();
    void formattingOnlyIsItsOwnAnswer();
    void aPictureAddedRemovedAndReplaced();
    void anEditedTableCellIsOneChangedBlock();
    void aListItemAddedLeavesTheOthersAlone();
    void piecesJoinBackIntoTheNewText();
};

namespace {

QString paragraphs(const QStringList& texts)
{
    QString html = QStringLiteral("<html><body>");
    for (const QString& text : texts) {
        html += QStringLiteral("<p>%1</p>").arg(text);
    }
    return html + QStringLiteral("</body></html>");
}

// The changes that are not "unchanged", which is what every assertion here
// is about.
QList<DocumentDiff::BlockChange> marked(const DocumentDiff::Result& result)
{
    QList<DocumentDiff::BlockChange> marks;
    for (const DocumentDiff::BlockChange& change : result.changes) {
        if (change.kind != Kind::Unchanged) {
            marks.append(change);
        }
    }
    return marks;
}

QString textOf(const DocumentDiff::BlockChange& change, Mark mark)
{
    QString text;
    for (const DocumentDiff::Piece& piece : change.pieces) {
        if (piece.mark == mark) {
            text += piece.text;
        }
    }
    return text.trimmed();
}

// What this step would have been if the blocks were compared where they sit
// instead of being matched up first — the mistake the spec asks to be ruled
// out (§7). It lives here, in the test, and nowhere near the program.
DocumentDiff::Result naiveCompare(const QString& oldContent, const QString& newContent)
{
    const QList<DocumentBlock> olds = DocumentBlocks::split(oldContent);
    const QList<DocumentBlock> news = DocumentBlocks::split(newContent);

    DocumentDiff::Result result;
    for (int index = 0; index < qMax(olds.size(), news.size()); ++index) {
        DocumentDiff::BlockChange change;
        if (index >= olds.size()) {
            change.kind     = Kind::Added;
            change.newIndex = index;
        } else if (index >= news.size()) {
            change.kind           = Kind::Removed;
            change.beforeNewIndex = news.size();
            change.removedText    = olds.at(index).text;
        } else if (olds.at(index).text == news.at(index).text) {
            change.kind     = Kind::Unchanged;
            change.newIndex = index;
        } else {
            change.kind     = Kind::Changed;
            change.newIndex = index;
            change.pieces = DocumentDiff::comparePieces(olds.at(index).text, news.at(index).text);
        }
        result.changes.append(change);
    }
    return result;
}

} // namespace

void TestDocumentDiff::theSameContentHasNoChanges()
{
    const QString content = paragraphs({QStringLiteral("Muster at station 3."),
                                        QStringLiteral("Wear a lifejacket.")});

    const DocumentDiff::Result result = DocumentDiff::compare(content, content);

    QVERIFY(result.isUnchanged());
    QCOMPARE(result.changes.size(), 2);
    QVERIFY(marked(result).isEmpty());
}

void TestDocumentDiff::oneChangedWordMarksOnlyThatWord()
{
    const QString before = paragraphs({QStringLiteral("Fire drills are held monthly.")});
    const QString after  = paragraphs({QStringLiteral("Fire drills are held quarterly.")});

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);
    const auto                 marks  = marked(result);

    QCOMPARE(marks.size(), 1);
    QCOMPARE(marks.first().kind, Kind::Changed);
    QCOMPARE(textOf(marks.first(), Mark::Removed), QStringLiteral("monthly."));
    QCOMPARE(textOf(marks.first(), Mark::Added), QStringLiteral("quarterly."));
    // The four words that did not change are kept, not re-marked.
    QCOMPARE(textOf(marks.first(), Mark::Kept), QStringLiteral("Fire drills are held"));
}

void TestDocumentDiff::aParagraphInsertedInTheMiddleIsOneInsertion()
{
    // §7's headline case. Everything after the new paragraph has moved down
    // one place, and none of it has changed.
    const QString before = paragraphs({QStringLiteral("Chapter 1 scope."),
                                       QStringLiteral("Chapter 2 responsibilities."),
                                       QStringLiteral("Chapter 3 records.")});
    const QString after  = paragraphs({QStringLiteral("Chapter 1 scope."),
                                       QStringLiteral("Chapter 1A definitions."),
                                       QStringLiteral("Chapter 2 responsibilities."),
                                       QStringLiteral("Chapter 3 records.")});

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);
    const auto                 marks  = marked(result);

    QCOMPARE(marks.size(), 1);
    QCOMPARE(marks.first().kind, Kind::Added);
    QCOMPARE(marks.first().newIndex, 1);

    // And the three that were already there are reported, untouched.
    QCOMPARE(result.countOf(Kind::Unchanged), 3);
    QCOMPARE(result.countOf(Kind::Changed), 0);
    QCOMPARE(result.countOf(Kind::Removed), 0);
}

void TestDocumentDiff::aNaivePositionByPositionComparisonGetsThatWrong()
{
    // The proof §7 asks for: the previous test is not passing by luck. The
    // same two pages, compared where the blocks sit instead of matched up
    // first, report the two untouched chapters as changed and the last one
    // as added — four marks instead of one, and three of them wrong.
    const QString before = paragraphs({QStringLiteral("Chapter 1 scope."),
                                       QStringLiteral("Chapter 2 responsibilities."),
                                       QStringLiteral("Chapter 3 records.")});
    const QString after  = paragraphs({QStringLiteral("Chapter 1 scope."),
                                       QStringLiteral("Chapter 1A definitions."),
                                       QStringLiteral("Chapter 2 responsibilities."),
                                       QStringLiteral("Chapter 3 records.")});

    const DocumentDiff::Result naive = naiveCompare(before, after);

    QCOMPARE(naive.countOf(Kind::Changed), 2);
    QCOMPARE(naive.countOf(Kind::Added), 1);
    QVERIFY2(marked(naive).size() > marked(DocumentDiff::compare(before, after)).size(),
             "the sequence matching is what keeps the untouched chapters clean");

    // Specifically: it claims the untouched Chapter 2 paragraph lost the word
    // "responsibilities" and gained "definitions", because Chapter 1A now
    // sits where Chapter 2 used to.
    const auto naiveMarks = marked(naive);
    QCOMPARE(naiveMarks.first().kind, Kind::Changed);
    QVERIFY2(textOf(naiveMarks.first(), Mark::Removed).contains(QStringLiteral("responsibilities")),
             "the naive answer rewrites a paragraph nobody touched");
    QVERIFY(textOf(naiveMarks.first(), Mark::Added).contains(QStringLiteral("definitions")));

    // The real comparison leaves that same paragraph alone, which is the
    // whole point.
    const DocumentDiff::Result real = DocumentDiff::compare(before, after);
    const DocumentDiff::BlockChange& chapterTwo = real.changes.at(2);
    QCOMPARE(chapterTwo.kind, Kind::Unchanged);
    QCOMPARE(chapterTwo.newIndex, 2);
}

void TestDocumentDiff::aParagraphRemovedFromTheMiddleIsOneRemoval()
{
    const QString before = paragraphs({QStringLiteral("Keep the deck clear."),
                                       QStringLiteral("Report every near miss."),
                                       QStringLiteral("Sign the log.")});
    const QString after  = paragraphs({QStringLiteral("Keep the deck clear."),
                                       QStringLiteral("Sign the log.")});

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);
    const auto                 marks  = marked(result);

    QCOMPARE(marks.size(), 1);
    QCOMPARE(marks.first().kind, Kind::Removed);
    QCOMPARE(marks.first().removedText, QStringLiteral("Report every near miss."));
    // It stood before "Sign the log.", which is block 1 of the new version —
    // which is how the view knows where to show it struck through.
    QCOMPARE(marks.first().beforeNewIndex, 1);
    QCOMPARE(result.countOf(Kind::Unchanged), 2);
}

void TestDocumentDiff::anUnrelatedParagraphReadsAsARemovalAndAnAddition()
{
    // §4.3's own words: a wholesale replacement is shown whole rather than
    // word by word, because marking every word of two unrelated paragraphs
    // tells a reader less than showing them both.
    const QString before = paragraphs({
        QStringLiteral("The master shall ensure that all crew attend the weekly safety "
                       "meeting and that minutes are kept."),
    });
    const QString after = paragraphs({
        QStringLiteral("Bunkering operations require a completed checklist signed by the "
                       "chief engineer before any transfer begins."),
    });

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);

    QCOMPARE(result.countOf(Kind::Removed), 1);
    QCOMPARE(result.countOf(Kind::Added), 1);
    QCOMPARE(result.countOf(Kind::Changed), 0);
}

void TestDocumentDiff::formattingOnlyIsItsOwnAnswer()
{
    // Same words, made bold. Matched on the text, so it is not a removal and
    // an addition of the same sentence (§4.3 item 4).
    const QString before =
        QStringLiteral("<html><body><p>Do not enter an enclosed space alone.</p></body></html>");
    const QString after = QStringLiteral(
        "<html><body><p><b>Do not enter an enclosed space alone.</b></p></body></html>");

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);
    const auto                 marks  = marked(result);

    QCOMPARE(marks.size(), 1);
    QCOMPARE(marks.first().kind, Kind::FormattingOnly);
    QCOMPARE(marks.first().newIndex, 0);
    // Nothing was added or removed: there is no text change to show.
    QVERIFY(marks.first().pieces.isEmpty());

    // A heading level is formatting too, by the same rule.
    const QString asHeading =
        QStringLiteral("<html><body><h2>Do not enter an enclosed space alone.</h2></body></html>");
    QCOMPARE(marked(DocumentDiff::compare(before, asHeading)).first().kind,
             Kind::FormattingOnly);
}

void TestDocumentDiff::aPictureAddedRemovedAndReplaced()
{
    const QString withoutPicture =
        QStringLiteral("<html><body><p>Muster list.</p></body></html>");
    const QString withPicture = QStringLiteral(
        "<html><body><p>Muster list.</p><p><img src=\"doc-image:aaa\"></p></body></html>");
    const QString withAnother = QStringLiteral(
        "<html><body><p>Muster list.</p><p><img src=\"doc-image:bbb\"></p></body></html>");

    const auto added = marked(DocumentDiff::compare(withoutPicture, withPicture));
    QCOMPARE(added.size(), 1);
    QCOMPARE(added.first().kind, Kind::Added);
    QVERIFY(added.first().isPicture);
    QCOMPARE(added.first().pictureName, QStringLiteral("doc-image:aaa"));

    const auto removed = marked(DocumentDiff::compare(withPicture, withoutPicture));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed.first().kind, Kind::Removed);
    QVERIFY(removed.first().isPicture);
    QCOMPARE(removed.first().oldPictureName, QStringLiteral("doc-image:aaa"));

    // Replaced: one change, naming both, so the view can label the box.
    const auto replaced = marked(DocumentDiff::compare(withPicture, withAnother));
    QCOMPARE(replaced.size(), 1);
    QCOMPARE(replaced.first().kind, Kind::Changed);
    QVERIFY(replaced.first().isPicture);
    QCOMPARE(replaced.first().oldPictureName, QStringLiteral("doc-image:aaa"));
    QCOMPARE(replaced.first().pictureName, QStringLiteral("doc-image:bbb"));
    QVERIFY2(replaced.first().pieces.isEmpty(), "a picture has no words to compare");
}

void TestDocumentDiff::anEditedTableCellIsOneChangedBlock()
{
    // A table's cells are blocks in reading order, so an edited cell is one
    // changed block and the rest of the table is untouched.
    const QString before = QStringLiteral(
        "<html><body><table><tr><td>Drill</td><td>Monthly</td></tr>"
        "<tr><td>Inspection</td><td>Annual</td></tr></table></body></html>");
    const QString after = QStringLiteral(
        "<html><body><table><tr><td>Drill</td><td>Quarterly</td></tr>"
        "<tr><td>Inspection</td><td>Annual</td></tr></table></body></html>");

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);
    const auto                 marks  = marked(result);

    QCOMPARE(marks.size(), 1);
    QCOMPARE(marks.first().kind, Kind::Changed);
    QCOMPARE(textOf(marks.first(), Mark::Removed), QStringLiteral("Monthly"));
    QCOMPARE(textOf(marks.first(), Mark::Added), QStringLiteral("Quarterly"));
}

void TestDocumentDiff::aListItemAddedLeavesTheOthersAlone()
{
    const QString before = QStringLiteral(
        "<html><body><ul><li>Lifejacket</li><li>Immersion suit</li></ul></body></html>");
    const QString after  = QStringLiteral("<html><body><ul><li>Lifejacket</li>"
                                          "<li>Whistle</li><li>Immersion suit</li></ul></body></html>");

    const DocumentDiff::Result result = DocumentDiff::compare(before, after);
    const auto                 marks  = marked(result);

    QCOMPARE(marks.size(), 1);
    QCOMPARE(marks.first().kind, Kind::Added);
    QCOMPARE(marks.first().newIndex, 1);
    QCOMPARE(result.countOf(Kind::Unchanged), 2);
}

void TestDocumentDiff::piecesJoinBackIntoTheNewText()
{
    // The view marks the pieces in place, so the kept and added pieces put
    // back together have to be exactly the new text — otherwise a comparison
    // would quietly rewrite the page it is showing.
    const QString oldText = QStringLiteral("Record the time, the place and the names.");
    const QString newText = QStringLiteral("Record the date, the place, the names and the ranks.");

    QString rebuilt;
    QString oldAgain;
    for (const DocumentDiff::Piece& piece : DocumentDiff::comparePieces(oldText, newText)) {
        if (piece.mark != Mark::Removed) {
            rebuilt += piece.text;
        }
        if (piece.mark != Mark::Added) {
            oldAgain += piece.text;
        }
    }

    QCOMPARE(rebuilt, newText);
    QCOMPARE(oldAgain, oldText);
}

QTEST_MAIN(TestDocumentDiff)
#include "tst_DocumentDiff.moc"
