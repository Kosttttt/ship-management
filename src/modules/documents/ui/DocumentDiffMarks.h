#pragma once

#include "modules/documents/domain/DocumentDiff.h"

#include <QColor>
#include <QString>
#include <QTextCharFormat>

class QTextDocument;

// What a comparison's marks look like, and how one is put on a page
// (document-history-spec.md §4.2).
//
// Split from DocumentDiffRenderer, which was 326 lines, by the rule in
// CLAUDE.md §9 — and along a seam that earns its keep rather than one that
// merely fits: that file decides **where** each mark goes by walking the
// comparison, and this one decides **what** a mark is. Step 5 has to put the
// same changes on a printed page, where "added text" is not a green
// background but a bar in the margin; when it comes, it replaces this file
// and keeps the walk.
namespace DocumentDiffMarks {

// §4.2's three colours, in one place. Light enough to read black text
// through, because they go behind the page's own words.
QColor addedBackground();
QColor removedForeground();
QColor formattingMark();

QTextCharFormat addedFormat();
QTextCharFormat removedFormat();
QTextCharFormat noteFormat();

// The words that go on the page. Here so that a test can look for them and
// so the printed output says the same thing.
QString formattingNote();
QString pictureLabel(const DocumentDiff::BlockChange& change);

// Something to insert, and where.
//
// Insertions are collected rather than done as they are found, because each
// one moves everything after it: they are applied from the end of the
// document backwards, which is the only order in which a position worked out
// earlier is still the right one.
struct Insertion {
    enum class What {
        RemovedWords,   // words only the old version had, struck through in place
        RemovedBlock,   // a whole paragraph only the old version had
        RemovedPicture, // a picture only the old version had, shown in a box
        MarginBar,      // the bar beside a paragraph whose formatting changed
        FormattingNote, // and the words that say so
        PictureBox,     // a picture in this document, added or replaced
    };

    int     position = 0;
    What    what     = What::RemovedWords;
    QString text;
    QString pictureName;
    QColor  colour;
};

// Adds `format` to what is already on those characters rather than replacing
// it, so a changed word that was bold stays bold and gains the green.
void mergeFormat(QTextDocument* document, int position, int length,
                 const QTextCharFormat& format);

// Makes room beside a paragraph for the margin bar.
void indentForBar(QTextDocument* document, int position);

void applyInsertion(QTextDocument* document, const Insertion& insertion);

} // namespace DocumentDiffMarks
