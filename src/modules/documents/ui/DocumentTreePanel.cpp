#include "modules/documents/ui/DocumentTreePanel.h"

#include "modules/documents/data/DocumentPageMoves.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/service/DocumentPublisher.h"
#include "modules/documents/ui/DocumentTreeActions.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>
#include <optional>

namespace {

constexpr int kPageIdRole = Qt::UserRole + 1;

} // namespace

DocumentTreePanel::DocumentTreePanel(DocumentPageRepository& pages, DocumentPageMoves& moves,
                                     DocumentPageRemovals&  removals,
                                     DocumentRevisionStore& revisions,
                                     DocumentPublisher& publisher, QWidget* parent)
    : QWidget(parent)
    , m_pages(pages)
    , m_moves(moves)
    , m_removals(removals)
    , m_revisions(revisions)
    , m_publisher(publisher)
    , m_actions(std::make_unique<DocumentTreeActions>(pages, moves, removals, revisions,
                                                     publisher, this))
{
    auto* heading     = new QLabel(tr("PAGES"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("documentPageTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    m_newButton      = new QPushButton(tr("New &Page"), this);
    m_upButton       = new QPushButton(tr("▲"), this);
    m_downButton     = new QPushButton(tr("▼"), this);
    m_moveToButton   = new QPushButton(tr("Move &to…"), this);
    m_deleteButton   = new QPushButton(tr("&Delete…"), this);
    m_withdrawButton = new QPushButton(tr("Undo &withdraw"), this);
    m_publishButton  = new QPushButton(tr("Publish re&vision…"), this);

    m_newButton->setObjectName(QStringLiteral("newPageButton"));
    m_upButton->setObjectName(QStringLiteral("movePageUpButton"));
    m_downButton->setObjectName(QStringLiteral("movePageDownButton"));
    m_moveToButton->setObjectName(QStringLiteral("movePageToButton"));
    m_deleteButton->setObjectName(QStringLiteral("deletePageButton"));
    m_withdrawButton->setObjectName(QStringLiteral("undoWithdrawButton"));
    m_publishButton->setObjectName(QStringLiteral("publishRevisionButton"));

    m_upButton->setToolTip(tr("Move up"));
    m_downButton->setToolTip(tr("Move down"));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_newButton);
    row->addWidget(m_upButton);
    row->addWidget(m_downButton);
    row->addWidget(m_moveToButton);
    row->addWidget(m_deleteButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading);
    layout->addWidget(m_tree, 1);
    layout->addLayout(row);
    layout->addWidget(m_withdrawButton);
    layout->addWidget(m_publishButton);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) {
        updateButtons();
        emit pageSelected(selectedPageId());
    });
    // Each button asks the user something, which DocumentTreeActions owns.
    // It returns true when something changed, so the reload is here and in
    // one place.
    connect(m_newButton, &QPushButton::clicked, this, [this] {
        if (m_actions->createPage(m_space, selectedPageId())) {
            reload();
        }
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this] {
        const QString pageId = selectedPageId();
        if (m_actions->deleteOrWithdraw(m_space, pageId, m_titles.value(pageId))) {
            reload();
        }
    });
    connect(m_withdrawButton, &QPushButton::clicked, this, [this] {
        if (m_actions->undoWithdraw(selectedPageId())) {
            reload();
        }
    });
    connect(m_moveToButton, &QPushButton::clicked, this, [this] {
        if (m_actions->moveTo(selectedPageId(), m_loaded, m_titles)) {
            reload();
        }
    });
    connect(m_publishButton, &QPushButton::clicked, this, [this] {
        if (m_actions->publishManual(m_space)) {
            reload();
            emit published();
        }
    });

    connect(m_upButton, &QPushButton::clicked, this, [this] {
        if (m_moves.moveUp(selectedPageId())) {
            reload();
        } else {
            QMessageBox::warning(this, tr("Could not move the page"), m_moves.errorString());
        }
    });
    connect(m_downButton, &QPushButton::clicked, this, [this] {
        if (m_moves.moveDown(selectedPageId())) {
            reload();
        } else {
            QMessageBox::warning(this, tr("Could not move the page"), m_moves.errorString());
        }
    });
}

DocumentTreePanel::~DocumentTreePanel() = default;

void DocumentTreePanel::setSpace(const DocumentSpace& space, bool editable)
{
    m_space    = space;
    m_editable = editable;
    reload();
}

QString DocumentTreePanel::titleFor(const DocumentPage& page) const
{
    if (page.hasDraft()) {
        return page.draftTitle;
    }

    // Published: its title is its latest revision's. Asked here rather than
    // stored on the page row, because the page row holds only the draft
    // (§4.2).
    std::optional<DocumentPageRevision> published;
    const_cast<DocumentRevisionStore&>(m_revisions).latestPageRevision(page.id, &published);
    return published.has_value() ? published->title : page.draftTitle;
}

void DocumentTreePanel::reload()
{
    const QString previous = selectedPageId();

    m_loaded.clear();
    m_titles.clear();

    if (!m_space.id.isEmpty() && !m_pages.listTree(m_space.id, &m_loaded)) {
        QMessageBox::warning(this, tr("Could not read the pages"), m_pages.errorString());
    }

    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    // The list arrives with every parent before its own children, so one
    // pass fills the tree and nothing has to be looked up twice.
    QHash<QString, QTreeWidgetItem*> items;
    QTreeWidgetItem*                 restore = nullptr;

    for (const DocumentPage& page : m_loaded) {
        const QString title = titleFor(page);
        m_titles.insert(page.id, title);

        QTreeWidgetItem* parent = items.value(page.parentId, nullptr);
        auto* item = parent == nullptr ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(parent);

        item->setText(0, title);
        item->setData(0, kPageIdRole, page.id);

        // §5: a small marker for an unpublished change, so somebody scanning
        // the tree can see what is still in draft without opening pages.
        if (page.hasDraft()) {
            item->setText(0, title + QStringLiteral(" •"));
            item->setToolTip(0, tr("Has unpublished changes"));
        }
        // §6.3: struck through until the manual's next revision removes it.
        if (page.withdrawPending) {
            QFont font = item->font(0);
            font.setStrikeOut(true);
            item->setFont(0, font);
            item->setToolTip(0, tr("To be withdrawn in the next revision"));
        }

        items.insert(page.id, item);
        if (page.id == previous) {
            restore = item;
        }
    }

    m_tree->expandAll();
    if (restore != nullptr) {
        m_tree->setCurrentItem(restore);
    } else if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }

    updateButtons();
    emit pageSelected(selectedPageId());
}

QString DocumentTreePanel::selectedPageId() const
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    return item == nullptr ? QString() : item->data(0, kPageIdRole).toString();
}

QStringList DocumentTreePanel::trailFor(const QString& pageId) const
{
    QStringList trail;
    QString     current = pageId;

    // Up through the parents, then reversed: the breadcrumb reads from the
    // space down to the page (§5).
    for (int guard = 0; guard < 1000 && !current.isEmpty(); ++guard) {
        const DocumentPage* found = nullptr;
        for (const DocumentPage& page : m_loaded) {
            if (page.id == current) {
                found = &page;
                break;
            }
        }
        if (found == nullptr) {
            break;
        }
        trail.prepend(m_titles.value(found->id, found->draftTitle));
        current = found->parentId;
    }

    if (!m_space.name.isEmpty()) {
        trail.prepend(m_space.name);
    }
    return trail;
}

void DocumentTreePanel::updateButtons()
{
    const bool hasSpace = !m_space.id.isEmpty();
    const bool hasPage  = !selectedPageId().isEmpty();

    m_newButton->setEnabled(hasSpace && m_editable);
    m_upButton->setEnabled(hasPage && m_editable);
    m_downButton->setEnabled(hasPage && m_editable);
    m_moveToButton->setEnabled(hasPage && m_editable);
    m_deleteButton->setEnabled(hasPage && m_editable);

    // Undo withdraw only exists while something is waiting to be withdrawn.
    bool withdrawing = false;
    for (const DocumentPage& page : m_loaded) {
        if (page.id == selectedPageId()) {
            withdrawing = page.withdrawPending;
        }
    }
    m_withdrawButton->setVisible(withdrawing);
    m_withdrawButton->setEnabled(m_editable);

    // §6.2: only a Manual space publishes as a whole.
    m_publishButton->setVisible(hasSpace && m_space.isManual());
    m_publishButton->setEnabled(m_editable);
}

