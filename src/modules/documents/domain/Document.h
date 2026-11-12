#pragma once

#include <QDate>
#include <QString>

// The four kinds of document this module holds
// (document-management-spec.md §1). A broad bucket for the category tabs and
// for where the file is filed on disk, not authoritative data.
enum class DocumentCategory {
    // "Not chosen yet" has to be representable so a form can start with
    // nothing selected and the repository can reject it. It is never stored:
    // migration 012's CHECK permits only the four real codes.
    Unset,

    Sms,         // SMS manuals and procedures, under ISM document control
    Circular,    // company circulars, flag and class notices
    PlanManual,  // approved plans, drawings, makers' manuals
    Report       // PSC / flag / vetting inspection, survey and audit reports
};

QString          categoryToCode(DocumentCategory category);
DocumentCategory categoryFromCode(const QString& code);

// A document: the thing that exists and is referred to. One issue of it is a
// DocumentVersion below.
//
// Plain data, no Qt widgets and no SQL (CLAUDE.md §4 rule 1).
struct Document {
    QString id;

    // Empty means **fleet-wide** — a document belonging to the company rather
    // than to any one ship (§4.1). The database stores that as NULL; an empty
    // QString is how it reads back, and every piece of code here says
    // `isFleetWide()` rather than testing the string, so the two readings
    // cannot drift apart.
    QString vesselId;

    DocumentCategory category = DocumentCategory::Unset;

    QString reference; // optional: "SMS-03", "Circular 12/2026"
    QString title;     // required
    QString notes;     // optional

    bool isFleetWide() const { return vesselId.isEmpty(); }
};

// One issue of a document. Every version is kept and every version's file
// stays where it was written: a new version is a new file, never an overwrite
// (§4.2).
struct DocumentVersion {
    QString id;
    QString documentId;

    // 1, 2, 3… within one document. The highest is current; the rest are
    // superseded. Worked out when needed, never stored (CLAUDE.md §6.7).
    int sequence = 0;

    QString revisionLabel; // optional: "Rev 5"
    QDate   revisionDate;  // optional, information only — nothing is scheduled from it
    QString changeNote;    // optional one line: what changed

    // Relative to the documents folder, never absolute (CLAUDE.md §6.9).
    QString filePath;

    // What the file was called when it was chosen, so the Versions list can
    // show a person something they recognise.
    QString originalFileName;

    // What the list's "Current version" column shows: the label when there is
    // one, and "v<sequence>" when there is not. Here rather than in the
    // widget, because it is a rule about what the value *is*
    // (CLAUDE.md §4 rule 3) and two screens already need the same answer.
    QString displayLabel() const;
};
