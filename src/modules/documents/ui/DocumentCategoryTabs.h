#pragma once

#include "modules/documents/domain/Document.h"

#include <QHash>
#include <QObject>

class QTabBar;

// The row of category tabs above the documents table
// (document-management-spec.md §4.4): All | SMS | Circulars | Plans & manuals
// | Reports, each with a count.
//
// **No red marks**, unlike the certificate list's tabs: nothing in this
// module has a date that can fall due, so there is nothing for a colour to
// mean (§1 — no expiry or review tracking here).
//
// Its own class, not the certificate module's: a module may never include
// from another module's folders (CLAUDE.md §5). The two are similar enough
// to look like duplication and different enough that sharing them would mean
// a common class in core/ that knew about both modules' categories — which is
// worse than two small classes that each know one.
class DocumentCategoryTabs : public QObject
{
    Q_OBJECT

public:
    explicit DocumentCategoryTabs(QWidget* parent);

    // The bar itself, for the screen to place. Owned by the parent widget.
    QTabBar* bar() const;

    // The selected tab. Unset means All, which filters nothing — the same
    // reading the repository's own "no category" uses.
    DocumentCategory selected() const;

    // Labels and counts. A category missing from the map reads zero, so a
    // caller cannot leave a stale number on a tab by forgetting one.
    void updateCounts(const QHash<int, int>& countsByCategory);

    // The tabs in display order, All first.
    static QList<DocumentCategory> order();

signals:
    void selectionChanged();

private:
    QTabBar* m_bar = nullptr;
};
