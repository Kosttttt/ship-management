#include "modules/documents/ui/DocumentPageView.h"

#include "modules/documents/ui/DocumentEditorToolbar.h"
#include "modules/documents/ui/DocumentRichEdit.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

constexpr int kReadingPage = 0;
constexpr int kEditingPage = 1;

QString revisionLine(const DocumentPageState& state)
{
    if (!state.hasPublished) {
        // §5: a never-published page shows its draft, marked as such — so
        // nobody mistakes work in progress for the controlled document.
        return DocumentPageView::tr("Draft — not published");
    }
    return DocumentPageView::tr("Rev %1 · %2")
        .arg(state.displayNumber())
        .arg(state.published.publishedAtUtc.toLocalTime().toString(
            QStringLiteral("dd MMM yyyy")));
}

} // namespace

DocumentPageView::DocumentPageView(DocumentImageStore& images, QWidget* parent)
    : QWidget(parent)
    , m_images(images)
{
    m_breadcrumb = new QLabel(QString(), this);
    m_breadcrumb->setObjectName(QStringLiteral("documentBreadcrumb"));
    m_breadcrumb->setWordWrap(true);
    QFont crumbFont = m_breadcrumb->font();
    crumbFont.setBold(true);
    m_breadcrumb->setFont(crumbFont);

    m_revision = new QLabel(QString(), this);
    m_revision->setObjectName(QStringLiteral("documentRevisionLine"));
    m_revision->setEnabled(false); // renders as secondary text

    m_editButton = new QPushButton(tr("&Edit"), this);
    m_editButton->setObjectName(QStringLiteral("editPageButton"));

    m_publishButton = new QPushButton(tr("&Publish…"), this);
    m_publishButton->setObjectName(QStringLiteral("publishPageButton"));

    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addWidget(m_revision, 1);
    headerRow->addWidget(m_editButton);
    headerRow->addWidget(m_publishButton);

    // The unpublished-changes bar (§5). A frame so it reads as a strip across
    // the page rather than as more text.
    m_draftBar = new QFrame(this);
    m_draftBar->setObjectName(QStringLiteral("unpublishedChangesBar"));
    m_draftBar->setFrameShape(QFrame::StyledPanel);

    m_draftBarLabel  = new QLabel(tr("This page has unpublished changes."), m_draftBar);
    m_continueButton = new QPushButton(tr("&Continue editing"), m_draftBar);
    m_continueButton->setObjectName(QStringLiteral("continueEditingButton"));
    m_discardButton = new QPushButton(tr("&Discard draft"), m_draftBar);
    m_discardButton->setObjectName(QStringLiteral("discardDraftButton"));

    auto* barLayout = new QHBoxLayout(m_draftBar);
    barLayout->addWidget(m_draftBarLabel, 1);
    barLayout->addWidget(m_continueButton);
    barLayout->addWidget(m_discardButton);

    // Reading: a read-only editor rather than a label, so pictures, tables
    // and lists render exactly as they will when editing.
    m_reader = new DocumentRichEdit(m_images, this);
    m_reader->setObjectName(QStringLiteral("documentReader"));
    m_reader->setReadOnly(true);

    auto* editPage   = new QWidget(this);
    m_titleEdit      = new QLineEdit(editPage);
    m_titleEdit->setObjectName(QStringLiteral("pageTitleEdit"));
    m_editor = new DocumentRichEdit(m_images, editPage);
    m_editor->setObjectName(QStringLiteral("documentEditor"));
    m_toolbar = new DocumentEditorToolbar(m_editor, editPage);

    auto* saveButton = new QPushButton(tr("&Save"), editPage);
    saveButton->setObjectName(QStringLiteral("savePageButton"));
    auto* cancelButton = new QPushButton(tr("Cancel"), editPage);
    cancelButton->setObjectName(QStringLiteral("cancelEditButton"));

    auto* editButtons = new QHBoxLayout;
    editButtons->setContentsMargins(0, 0, 0, 0);
    editButtons->addStretch();
    editButtons->addWidget(saveButton);
    editButtons->addWidget(cancelButton);

    auto* editLayout = new QVBoxLayout(editPage);
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->addWidget(m_titleEdit);
    editLayout->addWidget(m_toolbar->bar());
    editLayout->addWidget(m_editor, 1);
    editLayout->addLayout(editButtons);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("documentPageStack"));
    m_stack->insertWidget(kReadingPage, m_reader);
    m_stack->insertWidget(kEditingPage, editPage);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_breadcrumb);
    layout->addLayout(headerRow);
    layout->addWidget(m_draftBar);
    layout->addWidget(m_stack, 1);

    connect(m_editButton, &QPushButton::clicked, this, &DocumentPageView::startEditing);
    connect(m_continueButton, &QPushButton::clicked, this, &DocumentPageView::startEditing);
    connect(m_discardButton, &QPushButton::clicked, this,
            &DocumentPageView::draftDiscardRequested);
    connect(m_publishButton, &QPushButton::clicked, this, &DocumentPageView::publishRequested);
    connect(saveButton, &QPushButton::clicked, this, &DocumentPageView::draftSaveRequested);
    connect(cancelButton, &QPushButton::clicked, this, &DocumentPageView::cancelEditing);

    showNothing(tr("Choose a space and a page on the left."));
}

void DocumentPageView::showNothing(const QString& message)
{
    m_state = DocumentPageState();
    m_space = DocumentSpace();

    m_breadcrumb->setText(message);
    m_revision->clear();
    m_editButton->setEnabled(false);
    m_publishButton->setVisible(false);
    m_draftBar->setVisible(false);
    m_reader->clear();
    m_stack->setCurrentIndex(kReadingPage);
}

void DocumentPageView::showPage(const DocumentPageState& state, const DocumentSpace& space,
                                const QStringList& trail, bool editable)
{
    m_state    = state;
    m_space    = space;
    m_editable = editable;

    // "SMS Manual › Chapter 5 › 5.2 Fire Drills" (§5).
    m_breadcrumb->setText(trail.join(QStringLiteral(" › ")));
    m_revision->setText(revisionLine(state));

    m_editButton->setEnabled(editable);
    m_editButton->setToolTip(
        editable ? QString()
                 : tr("This space is managed at the office and is read-only here."));

    // §6.2: a page of a manual has no Publish of its own — it is published
    // with the manual's next revision.
    m_publishButton->setVisible(editable && !space.isManual());
    m_publishButton->setEnabled(state.page.hasDraft());

    // Reading shows the **published** content. A never-published page has
    // only its draft, which is what it shows, marked as a draft (§5).
    if (state.hasPublished) {
        m_reader->setHtml(state.published.content);
    } else {
        m_reader->setHtml(state.page.draftContent);
    }

    m_stack->setCurrentIndex(kReadingPage);
    updateBar();
}

void DocumentPageView::updateBar()
{
    // The bar is about an unpublished change to something already published.
    // On a page that has never been published there is nothing to compare it
    // against — the revision line already says "Draft — not published".
    const bool show = m_state.hasPublished && m_state.page.hasDraft()
                      && m_stack->currentIndex() == kReadingPage;
    m_draftBar->setVisible(show);
    m_continueButton->setEnabled(m_editable);
    m_discardButton->setEnabled(m_editable);
}

void DocumentPageView::startEditing()
{
    if (!m_editable) {
        return;
    }

    // Edit starts from the draft when there is one, and from the published
    // content otherwise — which is what "after publishing, the draft is
    // cleared and the published content is what Edit starts from" means
    // (§6.2).
    if (m_state.page.hasDraft()) {
        m_titleEdit->setText(m_state.page.draftTitle);
        m_editor->setHtml(m_state.page.draftContent);
    } else if (m_state.hasPublished) {
        m_titleEdit->setText(m_state.published.title);
        m_editor->setHtml(m_state.published.content);
    } else {
        m_titleEdit->clear();
        m_editor->clear();
    }

    m_stack->setCurrentIndex(kEditingPage);
    updateBar();
    m_editor->setFocus();
}

void DocumentPageView::cancelEditing()
{
    // §5: Cancel leaves the page as it was — the draft on disk is untouched,
    // so a cancelled edit loses only what was typed since Save.
    m_stack->setCurrentIndex(kReadingPage);
    updateBar();
}

bool DocumentPageView::isEditing() const
{
    return m_stack->currentIndex() == kEditingPage;
}

QString DocumentPageView::draftTitle() const
{
    return m_titleEdit->text().trimmed();
}

QString DocumentPageView::draftContent() const
{
    return m_editor->toHtml();
}
