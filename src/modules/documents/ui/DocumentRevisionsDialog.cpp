#include "modules/documents/ui/DocumentRevisionsDialog.h"

#include "modules/documents/data/DocumentHistoryReader.h"
#include "modules/documents/ui/DocumentRevisionDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// What a page row carries: which revision of it to open, and whether it has
// one before it to be compared with.
constexpr int kRevisionIdRole = Qt::UserRole;
constexpr int kIsChangedRole  = Qt::UserRole + 1;

QString shownDate(const QDateTime& utc)
{
    return utc.toLocalTime().toString(QStringLiteral("dd MMM yyyy")); // CLAUDE.md §6.2
}

QString markOf(DocumentHistoryReader::RevisionPage::Kind kind)
{
    switch (kind) {
    case DocumentHistoryReader::RevisionPage::Kind::New:
        return DocumentRevisionsDialog::tr("new");
    case DocumentHistoryReader::RevisionPage::Kind::Changed:
        return DocumentRevisionsDialog::tr("changed");
    case DocumentHistoryReader::RevisionPage::Kind::Withdrawn:
        return DocumentRevisionsDialog::tr("withdrawn");
    }
    return QString();
}

} // namespace

DocumentRevisionsDialog::DocumentRevisionsDialog(DocumentHistoryReader& history,
                                                 DocumentImageStore&    images,
                                                 QWidget*               parent)
    : QDialog(parent)
    , m_history(history)
    , m_images(images)
{
    setObjectName(QStringLiteral("documentRevisionsDialog"));
    setModal(true);
    resize(700, 480);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("recordOfRevisionsTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Revision"), tr("What"), tr("Note")});
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_openButton    = new QPushButton(tr("&Open page revision"), this);
    m_compareButton = new QPushButton(tr("Compare with &previous"), this);
    m_openButton->setObjectName(QStringLiteral("openRevisionPageButton"));
    m_compareButton->setObjectName(QStringLiteral("compareRevisionPageButton"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(m_openButton);
    buttonRow->addWidget(m_compareButton);
    buttonRow->addStretch();

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tree, 1);
    layout->addLayout(buttonRow);
    layout->addWidget(closeBox);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &DocumentRevisionsDialog::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            &DocumentRevisionsDialog::openSelected);
    connect(m_openButton, &QPushButton::clicked, this, &DocumentRevisionsDialog::openSelected);
    connect(m_compareButton, &QPushButton::clicked, this,
            &DocumentRevisionsDialog::compareSelected);
}

bool DocumentRevisionsDialog::showFor(const DocumentSpace& space)
{
    m_space = space;
    setWindowTitle(tr("Record of Revisions — %1").arg(space.name));

    QList<DocumentSpaceRevision> revisions;
    if (!m_history.listSpaceRevisions(space.id, &revisions)) {
        m_errorString = m_history.errorString();
        return false;
    }

    m_tree->clear();

    for (const DocumentSpaceRevision& revision : revisions) {
        auto* parent = new QTreeWidgetItem(m_tree);
        parent->setText(0, tr("Rev %1").arg(revision.number));
        parent->setText(1, shownDate(revision.publishedAtUtc));
        parent->setText(2, revision.note);
        parent->setFirstColumnSpanned(false);

        QList<DocumentHistoryReader::RevisionPage> pages;
        if (!m_history.listRevisionPages(revision.id, &pages)) {
            m_errorString = m_history.errorString();
            return false;
        }

        for (const DocumentHistoryReader::RevisionPage& page : pages) {
            auto* child = new QTreeWidgetItem(parent);
            child->setText(0, page.title);
            child->setText(1, markOf(page.kind));
            child->setData(0, kRevisionIdRole, page.pageRevisionId);
            // Only a changed page has something before it to compare with: a
            // new page's revision is its first, and a withdrawal records what
            // the page said when it went.
            child->setData(0, kIsChangedRole,
                           page.kind == DocumentHistoryReader::RevisionPage::Kind::Changed);
        }
        parent->setExpanded(true);
    }

    m_tree->header()->setStretchLastSection(true);
    m_tree->resizeColumnToContents(0);
    updateButtons();
    return true;
}

QString DocumentRevisionsDialog::selectedPageRevisionId() const
{
    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    if (selected.size() != 1) {
        return QString();
    }
    return selected.first()->data(0, kRevisionIdRole).toString();
}

bool DocumentRevisionsDialog::selectedIsChanged() const
{
    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    if (selected.size() != 1) {
        return false;
    }
    return selected.first()->data(0, kIsChangedRole).toBool();
}

void DocumentRevisionsDialog::updateButtons()
{
    const bool onAPage = !selectedPageRevisionId().isEmpty();
    m_openButton->setEnabled(onAPage);
    m_compareButton->setEnabled(onAPage && selectedIsChanged());
}

void DocumentRevisionsDialog::openSelected()
{
    const QString revisionId = selectedPageRevisionId();
    if (revisionId.isEmpty()) {
        return;
    }

    std::optional<DocumentPageRevision> revision;
    if (!m_history.findPageRevision(revisionId, &revision)) {
        QMessageBox::warning(this, tr("Could not read the revision"), m_history.errorString());
        return;
    }
    if (!revision.has_value()) {
        return;
    }

    DocumentRevisionDialog* dialog = DocumentRevisionDialog::view(
        tr("%1 — Rev %2 of %3")
            .arg(revision->title)
            .arg(revision->number)
            .arg(shownDate(revision->publishedAtUtc)),
        revision->title, revision->content, m_images, this);
    dialog->exec();
    dialog->deleteLater();
}

void DocumentRevisionsDialog::compareSelected()
{
    const QString revisionId = selectedPageRevisionId();
    if (revisionId.isEmpty() || !selectedIsChanged()) {
        return;
    }

    std::optional<DocumentPageRevision> revision;
    if (!m_history.findPageRevision(revisionId, &revision) || !revision.has_value()) {
        return;
    }

    std::optional<DocumentPageRevision> previous;
    if (!m_history.previousPageRevision(*revision, &previous)) {
        QMessageBox::warning(this, tr("Could not read the revision before it"),
                             m_history.errorString());
        return;
    }
    if (!previous.has_value()) {
        return;
    }

    DocumentRevisionDialog* dialog =
        DocumentRevisionDialog::compareRevisions(*previous, *revision, m_images, this);
    dialog->exec();
    dialog->deleteLater();
}
