#pragma once

#include <QString>

// One ship. A plain struct with no behaviour: it is carried between the
// repository and the screens and does nothing on its own
// (vessel-crud-spec §3).
//
// Field names follow the IMO Compendium spelling required by CLAUDE.md §9.
// The database columns are snake_case; the translation happens in
// VesselRepository, which is the only place that knows about columns at all.
struct Vessel {
    QString id;
    QString name;
    QString imoNumber;
    QString callSign;
    int     grossTonnage = 0;   // 0 means "not entered", never a float (CLAUDE.md §6.8)
    QString portOfRegistry;
    QString flagState;

    // Where this vessel's own documents are kept on this machine, chosen by
    // whoever runs the installation; empty means "not configured yet", which
    // refuses an attachment rather than falling back anywhere
    // (certificate-attachment-addendum-spec §2.2).
    //
    // Absolute, unlike every path stored against a certificate. Those are
    // relative so the files can travel (CLAUDE.md §6.9); this one says where
    // the root is here and now, and is re-chosen on another machine rather
    // than carried to it.
    //
    // Deliberately not written by VesselRepository::update(): see the note on
    // setAttachmentRootPath() for why.
    QString attachmentRootPath;
};
