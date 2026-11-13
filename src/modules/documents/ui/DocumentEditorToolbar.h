#pragma once

#include <QObject>

class DocumentRichEdit;
class QAction;
class QComboBox;
class QToolBar;

// The editor's toolbar (document-management-spec.md §6.1): paragraph style
// (Normal, Heading 1–3), bold, italic, underline, bulleted and numbered
// lists, insert table with add/remove row and column, insert picture,
// undo/redo.
//
// Its own class rather than sixty lines of QAction wiring inside the page
// view, which CLAUDE.md §9 would put over the guideline. The seam is real:
// everything here acts on the editor's cursor through QTextEdit's own API,
// and nothing here knows what a page, a space or a revision is.
class DocumentEditorToolbar : public QObject
{
    Q_OBJECT

public:
    // `editor` is what every action acts on; `parent` owns the bar.
    DocumentEditorToolbar(DocumentRichEdit* editor, QWidget* parent);

    QToolBar* bar() const;

private:
    void applyParagraphStyle(int index);
    void insertTable();
    void insertPicture();

    // Keeps the style box and the bold/italic/underline buttons showing what
    // the text under the cursor actually is, rather than what was last
    // clicked.
    void followCursor();

    DocumentRichEdit* m_editor = nullptr;
    QToolBar*         m_bar    = nullptr;
    QComboBox*        m_style  = nullptr;

    QAction* m_bold      = nullptr;
    QAction* m_italic    = nullptr;
    QAction* m_underline = nullptr;
};
