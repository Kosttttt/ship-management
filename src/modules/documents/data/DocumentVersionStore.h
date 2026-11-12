#pragma once

#include "modules/documents/domain/Document.h"

#include <QList>
#include <QString>

class QSqlDatabase;
class QSqlQuery;

// The SQL for document_version (document-management-spec.md §4.1).
//
// Split out of DocumentRepository, which was over the ~300-line guideline
// with both tables in it. A helper that holds SQL stays in the repository
// layer and is used only by its own repository, which is what CLAUDE.md §9
// asks for — and the seam is real: everything here is about one document's
// history, and nothing here decides who may change it. That is
// DocumentAccessRules, and the repository is what puts the two together.
//
// **No permission checks here.** Every caller is DocumentRepository, which
// has already asked whether this write is allowed. A second copy of that
// question in a second place is how two answers start to disagree.
class DocumentVersionStore
{
public:
    DocumentVersionStore(QSqlDatabase& database, const QString& nodeId);

    // The columns, and how to read them back — public and static because
    // DocumentRepository::list() joins this table to find each document's
    // current version, and the positional list must live in one place.
    //
    // Anything new goes on the END of the list, never into the middle:
    // inserting a column anywhere else shifts every field after it by one,
    // and nothing about that mistake looks wrong at a glance.
    static QString         columns();
    static DocumentVersion fromRow(const QSqlQuery& query, int offset);

    // Appends a version. `sequence` 0 or less means "work out the next one",
    // which is the ordinary case: the caller never chooses a number, so two
    // versions cannot share one.
    bool insert(const QString& documentId, const DocumentVersion& version, QString* newId);

    // Newest first, which is the order the Versions dialog shows them in and
    // which makes the first entry the current version.
    bool listFor(const QString& documentId, QList<DocumentVersion>* versions);

    // Soft-deletes every live version of one document, for when the document
    // itself is deleted. The files on disk are left exactly where they are
    // (§4.4).
    bool retireAllFor(const QString& documentId, const QString& nowUtc);

    QString errorString() const;

private:
    // One past the highest sequence this document has, **deleted versions
    // included**. A number is never reused, so a file named after a sequence
    // can never collide with one already in the folder.
    bool nextSequence(const QString& documentId, int* sequence);

    QSqlDatabase& m_database;
    QString       m_nodeId;
    QString       m_errorString;
};
