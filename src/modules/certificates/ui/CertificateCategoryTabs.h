#pragma once

#include "modules/certificates/service/CertificateListRows.h"

#include <QList>
#include <QObject>

class QTabBar;

// The row of category tabs above the certificate table
// (certificate-category-tabs-spec §1): All | Statutory | Class | Equipment |
// Other, each labelled with its count and turning red when something in it
// needs attention.
//
// Its own class rather than another sixty lines inside CertificateListWidget,
// which §4 of that spec asks for and which CLAUDE.md §9 would require anyway
// — that file is already over the guideline. The seam is a real one:
// everything here is about the tab bar, and it decides nothing. What the
// counts and colours are comes from CertificateListRows, a layer down.
//
// A QObject so Qt's parent-child ownership deletes it, like the other helper
// objects on that screen.
class CertificateCategoryTabs : public QObject
{
    Q_OBJECT

public:
    explicit CertificateCategoryTabs(QWidget* parent);

    // The bar itself, for the screen to place. Owned by the widget passed as
    // the parent, in the ordinary Qt way.
    QTabBar* bar() const;

    // The selected tab. Unset means All, which is what build() takes to mean
    // "no category filter".
    CertificateCategory selected() const;

    // Returns to All without emitting selectionChanged, and reports whether
    // that actually changed anything.
    //
    // Used by the alert banner's drill-down (§3), where the caller is about
    // to reload for its own reasons: the signal would make that two reloads,
    // and the return value tells the caller whether it still needs one.
    bool selectAllWithoutSignal();

    // Labels and colours from what the service counted. Entries are expected
    // in CertificateListRows::categoryOrder(), which is the order the tabs
    // were built in; anything else is ignored rather than drawn in the wrong
    // place.
    void updateLabels(const QList<CertificateListRows::CategoryTally>& tallies);

signals:
    // The user picked a different tab. Not emitted by
    // selectAllWithoutSignal().
    void selectionChanged();

private:
    QTabBar* m_bar = nullptr;
};
