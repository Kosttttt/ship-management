#include "modules/documents/ui/DocumentHistoryActions.h"

#include "modules/documents/data/DocumentHistoryReader.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/ui/DocumentHistoryDialog.h"
#include "modules/documents/ui/DocumentRevisionDialog.h"
#include "modules/documents/ui/DocumentRevisionsDialog.h"

#include <QCoreApplication>
#include <QMessageBox>

#include <optional>

namespace {

// tr() without being a QObject.
QString translated(const char* text)
{
    return QCoreApplication::translate("DocumentHistoryActions", text);
}

} // namespace

DocumentHistoryActions::DocumentHistoryActions(DocumentPageRepository& pages,
                                               DocumentRevisionStore&  revisions,
                                               DocumentHistoryReader&  history,
                                               DocumentRestorer&       restorer,
                                               DocumentImageStore& images, QWidget* dialogParent)
    : m_pages(pages)
    , m_revisions(revisions)
    , m_history(history)
    , m_restorer(restorer)
    , m_images(images)
    , m_parent(dialogParent)
{
}

bool DocumentHistoryActions::showHistory(const DocumentPageState& state,
                                         const DocumentSpace& space, const QString& pageTitle,
                                         bool editable)
{
    if (state.page.id.isEmpty()) {
        return false;
    }

    if (!state.hasPublished) {
        // Nothing has ever been published, so there is no history to look
        // through — said plainly rather than shown as an empty table.
        QMessageBox::information(
            m_parent, translated("No history yet"),
            translated("This page has never been published, so it has no revisions. "
                       "Publishing it is what makes its first one."));
        return false;
    }

    auto* dialog = new DocumentHistoryDialog(m_history, m_restorer, m_images, m_parent);
    if (!dialog->showFor(state.page, pageTitle, space.isManual(), editable)) {
        QMessageBox::warning(m_parent, translated("Could not read the history"),
                             dialog->errorString());
        dialog->deleteLater();
        return false;
    }

    dialog->exec();
    const bool restored = dialog->didRestore();
    dialog->deleteLater();
    return restored;
}

void DocumentHistoryActions::showRevisions(const DocumentSpace& space)
{
    if (space.id.isEmpty() || !space.isManual()) {
        return;
    }

    auto* dialog = new DocumentRevisionsDialog(m_history, m_images, m_parent);
    if (!dialog->showFor(space)) {
        QMessageBox::warning(m_parent, translated("Could not read the revisions"),
                             dialog->errorString());
        dialog->deleteLater();
        return;
    }
    dialog->exec();
    dialog->deleteLater();
}

void DocumentHistoryActions::compareDraftWithPublished(const QString& pageId)
{
    if (pageId.isEmpty()) {
        return;
    }

    std::optional<DocumentPage> page;
    if (!m_pages.findById(pageId, &page) || !page.has_value()) {
        return;
    }
    if (!page->hasDraft()) {
        return;
    }

    std::optional<DocumentPageRevision> published;
    if (!m_revisions.latestPageRevision(pageId, &published)) {
        QMessageBox::warning(m_parent, translated("Could not read the page"),
                             m_revisions.errorString());
        return;
    }
    if (!published.has_value()) {
        // A page that has never been published has nothing to compare
        // against: the whole of it is new.
        QMessageBox::information(
            m_parent, translated("Nothing to compare"),
            translated("This page has not been published yet, so everything on it is new."));
        return;
    }

    DocumentRevisionDialog* dialog = DocumentRevisionDialog::compareWithDraft(
        *published, page->draftTitle, page->draftContent, m_images, m_parent);
    dialog->exec();
    dialog->deleteLater();
}
