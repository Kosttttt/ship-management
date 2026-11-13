#include "modules/documents/ui/DocumentEditorToolbar.h"

#include "modules/documents/ui/DocumentRichEdit.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>
#include <QToolBar>

namespace {

// Heading sizes as multiples of the body text, so a page looks the same
// whatever font the program is running with — rather than fixed point sizes,
// which go wrong the moment the default changes.
constexpr double kHeading1Scale = 1.6;
constexpr double kHeading2Scale = 1.35;
constexpr double kHeading3Scale = 1.15;

enum class Style { Normal = 0, Heading1 = 1, Heading2 = 2, Heading3 = 3 };

} // namespace

DocumentEditorToolbar::DocumentEditorToolbar(DocumentRichEdit* editor, QWidget* parent)
    : QObject(parent)
    , m_editor(editor)
{
    m_bar = new QToolBar(parent);
    m_bar->setObjectName(QStringLiteral("documentEditorToolbar"));
    m_bar->setMovable(false);

    m_style = new QComboBox(m_bar);
    m_style->setObjectName(QStringLiteral("paragraphStyleCombo"));
    m_style->addItems({tr("Normal"), tr("Heading 1"), tr("Heading 2"), tr("Heading 3")});
    m_bar->addWidget(m_style);
    m_bar->addSeparator();

    // Checkable, so they show the state of the text under the cursor as well
    // as changing it.
    m_bold = m_bar->addAction(tr("B"));
    m_bold->setObjectName(QStringLiteral("boldAction"));
    m_bold->setToolTip(tr("Bold"));
    m_bold->setCheckable(true);

    m_italic = m_bar->addAction(tr("I"));
    m_italic->setObjectName(QStringLiteral("italicAction"));
    m_italic->setToolTip(tr("Italic"));
    m_italic->setCheckable(true);

    m_underline = m_bar->addAction(tr("U"));
    m_underline->setObjectName(QStringLiteral("underlineAction"));
    m_underline->setToolTip(tr("Underline"));
    m_underline->setCheckable(true);

    m_bar->addSeparator();

    QAction* bullets = m_bar->addAction(tr("• List"));
    bullets->setObjectName(QStringLiteral("bulletListAction"));

    QAction* numbers = m_bar->addAction(tr("1. List"));
    numbers->setObjectName(QStringLiteral("numberListAction"));

    m_bar->addSeparator();

    QAction* table = m_bar->addAction(tr("Table…"));
    table->setObjectName(QStringLiteral("insertTableAction"));

    QAction* addRow = m_bar->addAction(tr("+Row"));
    QAction* delRow = m_bar->addAction(tr("−Row"));
    QAction* addCol = m_bar->addAction(tr("+Col"));
    QAction* delCol = m_bar->addAction(tr("−Col"));

    m_bar->addSeparator();

    QAction* picture = m_bar->addAction(tr("Picture…"));
    picture->setObjectName(QStringLiteral("insertPictureAction"));

    m_bar->addSeparator();

    QAction* undo = m_bar->addAction(tr("Undo"));
    QAction* redo = m_bar->addAction(tr("Redo"));

    connect(m_style, &QComboBox::activated, this, &DocumentEditorToolbar::applyParagraphStyle);

    connect(m_bold, &QAction::triggered, this, [this](bool on) {
        m_editor->setFontWeight(on ? QFont::Bold : QFont::Normal);
    });
    connect(m_italic, &QAction::triggered, this,
            [this](bool on) { m_editor->setFontItalic(on); });
    connect(m_underline, &QAction::triggered, this,
            [this](bool on) { m_editor->setFontUnderline(on); });

    connect(bullets, &QAction::triggered, this, [this] {
        m_editor->textCursor().createList(QTextListFormat::ListDisc);
    });
    connect(numbers, &QAction::triggered, this, [this] {
        m_editor->textCursor().createList(QTextListFormat::ListDecimal);
    });

    connect(table, &QAction::triggered, this, &DocumentEditorToolbar::insertTable);

    // The four table edits all act on the table the cursor is in, and do
    // nothing when it is not in one — which is kinder than a disabled button
    // the user has to work out the reason for.
    connect(addRow, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_editor->textCursor();
        if (QTextTable* current = cursor.currentTable()) {
            current->insertRows(current->cellAt(cursor).row() + 1, 1);
        }
    });
    connect(delRow, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_editor->textCursor();
        if (QTextTable* current = cursor.currentTable()) {
            current->removeRows(current->cellAt(cursor).row(), 1);
        }
    });
    connect(addCol, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_editor->textCursor();
        if (QTextTable* current = cursor.currentTable()) {
            current->insertColumns(current->cellAt(cursor).column() + 1, 1);
        }
    });
    connect(delCol, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_editor->textCursor();
        if (QTextTable* current = cursor.currentTable()) {
            current->removeColumns(current->cellAt(cursor).column(), 1);
        }
    });

    connect(picture, &QAction::triggered, this, &DocumentEditorToolbar::insertPicture);
    connect(undo, &QAction::triggered, m_editor, &QTextEdit::undo);
    connect(redo, &QAction::triggered, m_editor, &QTextEdit::redo);

    connect(m_editor, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat&) { followCursor(); });
    connect(m_editor, &QTextEdit::cursorPositionChanged, this,
            &DocumentEditorToolbar::followCursor);

    followCursor();
}

QToolBar* DocumentEditorToolbar::bar() const
{
    return m_bar;
}

void DocumentEditorToolbar::applyParagraphStyle(int index)
{
    QTextCursor cursor = m_editor->textCursor();
    cursor.select(QTextCursor::BlockUnderCursor);

    QTextCharFormat format;
    switch (static_cast<Style>(index)) {
    case Style::Heading1:
        format.setFontWeight(QFont::Bold);
        format.setProperty(QTextFormat::FontSizeAdjustment, 3);
        format.setFontPointSize(m_editor->document()->defaultFont().pointSizeF()
                                * kHeading1Scale);
        break;
    case Style::Heading2:
        format.setFontWeight(QFont::Bold);
        format.setFontPointSize(m_editor->document()->defaultFont().pointSizeF()
                                * kHeading2Scale);
        break;
    case Style::Heading3:
        format.setFontWeight(QFont::Bold);
        format.setFontPointSize(m_editor->document()->defaultFont().pointSizeF()
                                * kHeading3Scale);
        break;
    case Style::Normal:
        // A fresh format with nothing set, which is what "no longer a
        // heading" has to mean: clearing only the size would leave it bold.
        format.setFontWeight(QFont::Normal);
        format.setFontPointSize(m_editor->document()->defaultFont().pointSizeF());
        break;
    }

    cursor.mergeCharFormat(format);
    m_editor->setFocus();
}

void DocumentEditorToolbar::insertTable()
{
    bool      accepted = false;
    const int rows =
        QInputDialog::getInt(m_bar, tr("Insert table"), tr("Rows:"), 2, 1, 100, 1, &accepted);
    if (!accepted) {
        return;
    }
    const int columns =
        QInputDialog::getInt(m_bar, tr("Insert table"), tr("Columns:"), 2, 1, 20, 1, &accepted);
    if (!accepted) {
        return;
    }

    QTextTableFormat format;
    format.setCellPadding(4);
    format.setCellSpacing(0);
    // A visible grid: a table in a procedure is usually a table of values,
    // and one without lines reads as a jumble of words.
    format.setBorder(1);
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);

    m_editor->textCursor().insertTable(rows, columns, format);
}

void DocumentEditorToolbar::insertPicture()
{
    // The formats this Qt build can actually read, asked rather than listed:
    // a filter naming something unsupported is a file the user can choose
    // and then be told off for.
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    }

    const QString chosen = QFileDialog::getOpenFileName(
        m_bar, tr("Insert picture"), QString(),
        tr("Pictures (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (chosen.isEmpty()) {
        return;
    }

    const QImage image(chosen);
    QString      error;
    if (!m_editor->insertStoredImage(image, &error)) {
        QMessageBox::warning(m_bar, tr("Could not insert the picture"),
                             error.isEmpty() ? tr("That file could not be read as a picture.")
                                             : error);
    }
}

void DocumentEditorToolbar::followCursor()
{
    const QTextCharFormat format = m_editor->currentCharFormat();

    m_bold->setChecked(format.fontWeight() >= QFont::DemiBold);
    m_italic->setChecked(format.fontItalic());
    m_underline->setChecked(format.fontUnderline());

    // The style box follows the size relative to the body text, which is how
    // the headings were applied in the first place.
    const double body = m_editor->document()->defaultFont().pointSizeF();
    const double size = format.fontPointSize() > 0 ? format.fontPointSize() : body;
    const double ratio = body > 0 ? size / body : 1.0;

    int index = static_cast<int>(Style::Normal);
    if (ratio >= kHeading1Scale - 0.05) {
        index = static_cast<int>(Style::Heading1);
    } else if (ratio >= kHeading2Scale - 0.05) {
        index = static_cast<int>(Style::Heading2);
    } else if (ratio >= kHeading3Scale - 0.05) {
        index = static_cast<int>(Style::Heading3);
    }

    const QSignalBlocker blocker(m_style);
    m_style->setCurrentIndex(index);
}
