#pragma once

#include "modules/documents/domain/DocumentPage.h"
#include "modules/documents/domain/DocumentSpace.h"

#include <QString>
#include <QStringList>
#include <QWidget>

class DocumentEditorToolbar;
class DocumentImageStore;
class DocumentRichEdit;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

// The right-hand side of the Documents screen (document-management-spec.md
// §5): a breadcrumb, the page's revision line, and then the page — read, or
// being edited.
//
// It decides nothing about what may be edited or what publishing means. It
// shows a page, reports that Save was pressed, and leaves the rest to the
// screen that owns it.
class DocumentPageView : public QWidget
{
    Q_OBJECT

public:
    DocumentPageView(DocumentImageStore& images, QWidget* parent = nullptr);

    // Shows one page. `trail` is the breadcrumb, ancestors first, ending with
    // this page's own title. `editable` greys Edit for a space this
    // installation may only read (§4.3).
    void showPage(const DocumentPageState& state, const DocumentSpace& space,
                  const QStringList& trail, bool editable);

    // Back to nothing — no space or page selected.
    void showNothing(const QString& message);

    bool isEditing() const;

    // What the editor currently holds, for the screen to save as the draft.
    QString draftTitle() const;
    QString draftContent() const;

signals:
    // Save was pressed in the editor: the screen writes the draft.
    void draftSaveRequested();

    // The unpublished-changes bar's Discard draft (§5).
    void draftDiscardRequested();

    // Publish… on a Collection page. A Manual space's pages have no Publish
    // of their own (§6.2), so this is only ever emitted for a Collection.
    void publishRequested();

private:
    void startEditing();
    void cancelEditing();
    void updateBar();

    DocumentImageStore& m_images;

    QLabel*         m_breadcrumb  = nullptr;
    QLabel*         m_revision    = nullptr;
    QPushButton*    m_editButton  = nullptr;
    QPushButton*    m_publishButton = nullptr;

    // The "This page has unpublished changes" bar, with Continue editing and
    // Discard draft (§5).
    QFrame*      m_draftBar        = nullptr;
    QLabel*      m_draftBarLabel   = nullptr;
    QPushButton* m_continueButton  = nullptr;
    QPushButton* m_discardButton   = nullptr;

    // Page 0 reads, page 1 edits. A stack rather than hiding widgets, so
    // there is never a moment when both are visible.
    QStackedWidget*        m_stack   = nullptr;
    DocumentRichEdit*      m_reader  = nullptr;
    DocumentRichEdit*      m_editor  = nullptr;
    DocumentEditorToolbar* m_toolbar = nullptr;
    QLineEdit*             m_titleEdit = nullptr;

    DocumentPageState m_state;
    DocumentSpace     m_space;
    bool              m_editable = false;
};
