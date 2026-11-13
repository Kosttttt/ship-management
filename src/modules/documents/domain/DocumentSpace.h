#pragma once

#include <QString>

// The two kinds of space (document-management-spec.md §2), which is the
// distinction the whole module turns on.
enum class SpaceKind {
    // "Not chosen yet", so a form can start with nothing selected and the
    // repository can refuse it. Never stored: migration 014's CHECK permits
    // only the two real codes.
    Unset,

    // The whole space gets Rev N, covering every page changed since Rev N−1,
    // with one note. ISM document control: only a published revision counts,
    // and a page shows the manual revision it last changed in.
    Manual,

    // Each page is published on its own and carries its own count. Circulars,
    // forms, and every vessel's own space.
    Collection
};

QString   kindToCode(SpaceKind kind);
SpaceKind kindFromCode(const QString& code);

// A space: a manual, a collection of circulars, or one vessel's own place to
// keep its pages.
//
// Plain data, no Qt widgets and no SQL (CLAUDE.md §4 rule 1).
struct DocumentSpace {
    QString   id;
    QString   name;
    SpaceKind kind = SpaceKind::Unset;

    // Set only for a vessel's automatic space; empty for a company space.
    // Every piece of code here asks `isVesselSpace()` rather than testing the
    // string, so the two readings cannot drift apart.
    QString vesselId;

    QString description;
    int     position = 0;

    bool isVesselSpace() const { return !vesselId.isEmpty(); }
    bool isManual() const { return kind == SpaceKind::Manual; }
};
