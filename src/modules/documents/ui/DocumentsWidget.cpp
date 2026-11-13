#include "modules/documents/ui/DocumentsWidget.h"

#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/service/DocumentPublisher.h"
#include "modules/documents/ui/DocumentPageView.h"
#include "modules/documents/ui/DocumentPublishDialog.h"
#include "modules/documents/ui/DocumentSpacePanel.h"
#include "modules/documents/ui/DocumentTreePanel.h"

#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <optional>

DocumentsWidget::DocumentsWidget(DocumentSpaceRepository& spaces, DocumentPageRepository& pages,
                                 DocumentPageMoves& moves, DocumentPageRemovals& removals,
                                 DocumentRevisionStore& revisions, DocumentPublisher& publisher,
                                 DocumentImageStore& images, QWidget* parent)
    : QWidget(parent)
    , m_spaces(spaces)
    , m_pages(pages)
    , m_revisions(revisions)
    , m_publisher(publisher)
{
    m_spacePanel = new DocumentSpacePanel(spaces, removals, this);
    m_treePanel  = new DocumentTreePanel(pages, moves, removals, revisions, publisher, this);
    m_pageView   = new DocumentPageView(images, this);

    // Spaces above the tree, both on the left, the page on the right (§5).
    // Splitters rather than fixed widths, because a manual's page titles are
    // long and how much room they need is the reader's business.
    auto* leftSplitter = new QSplitter(Qt::Vertical, this);
    leftSplitter->setObjectName(QStringLiteral("documentsLeftSplitter"));
    leftSplitter->addWidget(m_spacePanel);
    leftSplitter->addWidget(m_treePanel);
    leftSplitter->setStretchFactor(1, 1); // the tree grows, the spaces list does not

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("documentsSplitter"));
    splitter->addWidget(leftSplitter);
    splitter->addWidget(m_pageView);
    splitter->setStretchFactor(1, 1); // the page takes the surplus width
    splitter->setSizes({280, 820});

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(m_spacePanel, &DocumentSpacePanel::spaceSelected, this,
            &DocumentsWidget::spaceChanged);
    connect(m_treePanel, &DocumentTreePanel::pageSelected, this, &DocumentsWidget::pageChanged);
    connect(m_treePanel, &DocumentTreePanel::published, this, &DocumentsWidget::refreshPage);

    connect(m_pageView, &DocumentPageView::draftSaveRequested, this,
            &DocumentsWidget::saveDraft);
    connect(m_pageView, &DocumentPageView::draftDiscardRequested, this,
            &DocumentsWidget::discardDraft);
    connect(m_pageView, &DocumentPageView::publishRequested, this,
            &DocumentsWidget::publishPage);

    // The vessel spaces are already there: the module makes them just before
    // it builds this widget (§4.3).
    m_spacePanel->reload();
}

void DocumentsWidget::spaceChanged(const QString& spaceId)
{
    m_space = m_spacePanel->selectedSpace();

    // The space decides whether anything in it may be changed (§4.3). The
    // panels only grey their buttons; the repository is what refuses.
    const bool editable = !m_space.id.isEmpty() && m_spaces.mayEdit(m_space);
    m_treePanel->setSpace(m_space, editable);

    if (spaceId.isEmpty()) {
        m_pageView->showNothing(tr("Choose a space on the left."));
    }
}

void DocumentsWidget::pageChanged(const QString& pageId)
{
    m_pageId = pageId;
    refreshPage();
}

void DocumentsWidget::refreshPage()
{
    if (m_pageId.isEmpty()) {
        m_pageView->showNothing(m_space.id.isEmpty()
                                    ? tr("Choose a space on the left.")
                                    : tr("This space has no pages yet."));
        return;
    }

    std::optional<DocumentPage> page;
    if (!m_pages.findById(m_pageId, &page) || !page.has_value()) {
        m_pageView->showNothing(tr("This page is no longer there."));
        return;
    }

    // Which revision is current, and the manual's number for it — a rule, so
    // it is worked out in the repository layer and not here (§4.2).
    DocumentPageState state;
    if (!m_revisions.stateFor(*page, m_space.isManual(), &state)) {
        QMessageBox::warning(this, tr("Could not read the page"), m_revisions.errorString());
        return;
    }

    m_pageView->showPage(state, m_space, m_treePanel->trailFor(m_pageId),
                         m_spaces.mayEdit(m_space));
}

void DocumentsWidget::saveDraft()
{
    if (m_pageId.isEmpty()) {
        return;
    }

    if (!m_pages.saveDraft(m_pageId, m_pageView->draftTitle(), m_pageView->draftContent())) {
        QMessageBox::warning(this, tr("Could not save the page"), m_pages.errorString());
        return;
    }

    // The tree first: the title may have changed, and the marker for an
    // unpublished change has certainly appeared.
    m_treePanel->reload();
    refreshPage();
}

void DocumentsWidget::discardDraft()
{
    if (m_pageId.isEmpty()) {
        return;
    }

    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(tr("Discard draft?"));
    confirm.setText(tr("Throw away the unpublished changes to this page?"));
    confirm.setInformativeText(
        tr("The page goes back to its published version. This cannot be undone."));
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirm.setDefaultButton(QMessageBox::No);

    if (confirm.exec() != QMessageBox::Yes) {
        return;
    }

    if (!m_pages.clearDraft(m_pageId)) {
        QMessageBox::warning(this, tr("Could not discard the draft"), m_pages.errorString());
        return;
    }
    m_treePanel->reload();
    refreshPage();
}

void DocumentsWidget::publishPage()
{
    if (m_pageId.isEmpty()) {
        return;
    }

    std::optional<DocumentPage> page;
    if (!m_pages.findById(m_pageId, &page) || !page.has_value()) {
        return;
    }

    int next = 0;
    m_revisions.nextPageNumber(m_pageId, &next);

    DocumentPublishDialog* dialog =
        DocumentPublishDialog::forPage(page->draftTitle, next, this);
    const int     answer = dialog->exec();
    const QString note   = dialog->note();
    dialog->deleteLater();

    if (answer != QDialog::Accepted) {
        return;
    }

    if (!m_publisher.publishPage(m_pageId, note)) {
        QMessageBox::warning(this, tr("Could not publish the page"), m_publisher.errorString());
        return;
    }

    m_treePanel->reload();
    refreshPage();
}
