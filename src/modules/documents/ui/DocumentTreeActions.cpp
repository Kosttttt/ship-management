#include "modules/documents/ui/DocumentTreeActions.h"

#include "modules/documents/data/DocumentPageMoves.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/service/DocumentPublisher.h"
#include "modules/documents/ui/DocumentPublishDialog.h"

#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include <optional>

namespace {

// tr() without being a QObject.
QString translated(const char* text)
{
    return QCoreApplication::translate("DocumentTreeActions", text);
}

} // namespace

DocumentTreeActions::DocumentTreeActions(DocumentPageRepository& pages, DocumentPageMoves& moves,
                                         DocumentPageRemovals&  removals,
                                         DocumentRevisionStore& revisions,
                                         DocumentPublisher& publisher, QWidget* dialogParent)
    : m_pages(pages)
    , m_moves(moves)
    , m_removals(removals)
    , m_revisions(revisions)
    , m_publisher(publisher)
    , m_parent(dialogParent)
{
}

bool DocumentTreeActions::createPage(const DocumentSpace& space, const QString& parentId)
{
    bool          accepted = false;
    const QString title    = QInputDialog::getText(m_parent, translated("New page"),
                                                   translated("Title:"), QLineEdit::Normal,
                                                   QString(), &accepted);
    if (!accepted || title.trimmed().isEmpty()) {
        return false;
    }

    if (!m_pages.create(space.id, parentId, title, nullptr)) {
        QMessageBox::warning(m_parent, translated("Could not create the page"),
                             m_pages.errorString());
        return false;
    }
    return true;
}

bool DocumentTreeActions::moveTo(const QString& pageId, const QList<DocumentPage>& pages,
                                 const QHash<QString, QString>& titles)
{
    if (pageId.isEmpty()) {
        return false;
    }

    // Every other page of this space, plus the top of the tree. A page
    // cannot be moved under itself or its own child; the repository refuses
    // that, and offering it here would be offering a mistake.
    QStringList labels{translated("(top of the tree)")};
    QStringList ids{QString()};
    for (const DocumentPage& page : pages) {
        if (page.id == pageId) {
            continue;
        }
        labels.append(titles.value(page.id));
        ids.append(page.id);
    }

    bool          accepted = false;
    const QString chosen =
        QInputDialog::getItem(m_parent, translated("Move page"), translated("New parent:"),
                              labels, 0, false, &accepted);
    if (!accepted) {
        return false;
    }

    const int index = labels.indexOf(chosen);
    if (index < 0) {
        return false;
    }
    if (!m_moves.moveTo(pageId, ids.at(index))) {
        QMessageBox::warning(m_parent, translated("Could not move the page"),
                             m_moves.errorString());
        return false;
    }
    return true;
}

bool DocumentTreeActions::undoWithdraw(const QString& pageId)
{
    if (pageId.isEmpty()) {
        return false;
    }
    if (!m_removals.setWithdrawPending(pageId, false)) {
        QMessageBox::warning(m_parent, translated("Could not undo the withdrawal"),
                             m_removals.errorString());
        return false;
    }
    return true;
}

bool DocumentTreeActions::deleteOrWithdraw(const DocumentSpace& space, const QString& pageId,
                                           const QString& title)
{
    if (pageId.isEmpty()) {
        return false;
    }

    std::optional<DocumentPageRevision> published;
    if (!m_revisions.latestPageRevision(pageId, &published)) {
        QMessageBox::warning(m_parent, translated("Could not read the page"),
                             m_revisions.errorString());
        return false;
    }

    int descendants = 0;
    m_removals.countDescendants(pageId, &descendants);

    // §6.3, the three cases. A published page of a manual is not deleted at
    // all: it is marked for withdrawal, because removing a section is a
    // change to the manual and belongs in a revision like any other.
    if (published.has_value() && space.isManual()) {
        QMessageBox confirm(m_parent);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(translated("Withdraw page?"));
        confirm.setText(translated("Mark '%1' to be withdrawn?").arg(title));
        confirm.setInformativeText(translated(
            "It stays in the manual, struck through, until the next revision is published. "
            "That revision will list it as withdrawn.\n\nYou can undo this until then."));
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) {
            return false;
        }
        if (!m_removals.setWithdrawPending(pageId, true)) {
            QMessageBox::warning(m_parent, translated("Could not mark the page"),
                                 m_removals.errorString());
            return false;
        }
        return true;
    }

    if (published.has_value() || descendants > 0) {
        QMessageBox confirm(m_parent);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(translated("Delete page?"));
        confirm.setText(translated("Delete '%1'?").arg(title));
        confirm.setInformativeText(
            descendants > 0
                ? QCoreApplication::translate(
                      "DocumentTreeActions",
                      "Its %n page(s) beneath it go with it. Nothing is destroyed — deleted "
                      "pages and their revisions stay in the database.",
                      nullptr, descendants)
                : translated("Nothing is destroyed — a deleted page and its revisions stay in "
                             "the database."));
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) {
            return false;
        }
    }

    // A never-published page with nothing beneath it goes straight away
    // (§6.3): there is nothing to lose and nothing to warn about.
    if (!m_removals.softDeleteWithDescendants(pageId, nullptr)) {
        QMessageBox::warning(m_parent, translated("Could not delete the page"),
                             m_removals.errorString());
        return false;
    }
    return true;
}

bool DocumentTreeActions::publishManual(const DocumentSpace& space)
{
    if (space.id.isEmpty() || !space.isManual()) {
        return false;
    }

    QList<DocumentPublisher::PendingChange> changes;
    if (!m_publisher.pendingChanges(space.id, &changes)) {
        QMessageBox::warning(m_parent, translated("Could not work out what changed"),
                             m_publisher.errorString());
        return false;
    }
    if (changes.isEmpty()) {
        QMessageBox::information(m_parent, translated("Nothing to publish"),
                                 translated("No page has changed since the last revision."));
        return false;
    }

    int next = 0;
    m_revisions.nextSpaceNumber(space.id, &next);

    DocumentPublishDialog* dialog =
        DocumentPublishDialog::forManual(space.name, next, changes, m_parent);
    const int     answer = dialog->exec();
    const QString note   = dialog->note();
    dialog->deleteLater();

    if (answer != QDialog::Accepted) {
        return false;
    }

    if (!m_publisher.publishManual(space.id, note, nullptr)) {
        QMessageBox::warning(m_parent, translated("Could not publish the revision"),
                             m_publisher.errorString());
        return false;
    }
    return true;
}
