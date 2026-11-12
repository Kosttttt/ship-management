#pragma once

#include "core/InstallationContext.h"
#include "modules/documents/data/DocumentAccessRules.h"
#include "modules/documents/data/DocumentVersionStore.h"
#include "modules/documents/domain/Document.h"

#include <QList>
#include <QString>

#include <optional>

class QSqlDatabase;

// Reads and writes the document table, and puts the two halves of this
// module's storage together: who may do what (DocumentAccessRules) and one
// document's history (DocumentVersionStore).
//
// Those two are its own, in the same layer, and nothing outside this module
// uses either (CLAUDE.md §9). Every permission question is asked **here**,
// once, before anything is written — the version store does no checking of
// its own, so there is only ever one answer.
//
// **Who may see and change what is enforced in this layer, not in the
// screen** (document-management-spec.md §4.3, CLAUDE.md §3). A screen that
// forgot either rule would show one ship another ship's paperwork, or let a
// vessel quietly edit the company's SMS.
class DocumentRepository
{
public:
    DocumentRepository(QSqlDatabase& database, const InstallationContext& installation);

    // One row of the Documents screen (§4.4): the document, whichever version
    // is current, and how many it has.
    //
    // Read in one query rather than one per document. The certificate list
    // accepts an N+1 because each of its rows needs a calculation; nothing
    // here needs one, so there is no reason to pay for it.
    struct ListRow {
        Document                       document;
        std::optional<DocumentVersion> currentVersion;
        int                            versionCount = 0;
    };

    // Fleet-wide documents plus the given vessel's, which is what both
    // installation modes show. An empty vesselId gives the fleet-wide ones
    // alone — what OFFICE sees before a vessel is chosen in the toolbar.
    //
    // In VESSEL mode the argument is **ignored** in favour of this
    // installation's own vessel. Asking for another ship's documents by any
    // route returns that ship's nothing.
    bool list(const QString& vesselId, QList<ListRow>* rows);

    bool findById(const QString& id, std::optional<Document>* document);

    // The document and its first version together: a document with no version
    // has no file behind it and nothing to open, so the two are written in one
    // transaction and either both land or neither does.
    //
    // `firstVersion`'s sequence is ignored and set to 1 here.
    bool create(const Document& document, const DocumentVersion& firstVersion,
                QString* newDocumentId = nullptr);

    // Reference, title, notes and category. Deliberately not the scope: a
    // document that changed from fleet-wide to one vessel's, or between
    // vessels, would leave its files filed under the wrong folder, and moving
    // them is explicitly not in step 1 (§4.7).
    bool updateDetails(const Document& document);

    // Appends a version and makes it current, the previous one becoming
    // superseded. Which is which comes from the sequence and is never stored
    // (CLAUDE.md §6.7).
    bool addVersion(const QString& documentId, const DocumentVersion& version,
                    QString* newVersionId = nullptr);

    // Newest first. The first entry is the current version; the rest are the
    // archive.
    bool listVersions(const QString& documentId, QList<DocumentVersion>* versions);

    bool currentVersion(const QString& documentId, std::optional<DocumentVersion>* version);

    // Soft delete (CLAUDE.md §6.4), taking the document's versions with it.
    //
    // **The files stay in the documents folder** (§4.4). An SMS record is not
    // something to destroy because somebody clicked the wrong row, and a
    // deletion has to replicate to the other side as a row rather than as an
    // absence.
    bool softDelete(const QString& documentId);

    QString errorString() const;

private:
    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;

    DocumentAccessRules m_access;
    DocumentVersionStore m_versions;

    QString m_errorString;
};
