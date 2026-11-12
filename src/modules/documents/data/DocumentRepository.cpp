#include "modules/documents/data/DocumentRepository.h"

#include "core/TransactionScope.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent set by every other repository here.
const QString kSystemUser = QStringLiteral("SYSTEM");

// Positional, and read back positionally in documentFromRow(). Anything new
// goes on the END of this list, never into the middle.
const QString kDocumentColumns =
    QStringLiteral("d.id, d.vessel_id, d.category, d.reference, d.title, d.notes");

constexpr int kDocumentColumnCount = 6;

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

// An optional field left blank is stored as NULL rather than as an empty
// string, so "not entered" and "deliberately blank" cannot drift apart.
QVariant nullableText(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

Document documentFromRow(const QSqlQuery& query)
{
    Document document;
    document.id = query.value(0).toString();
    // NULL reads back as an empty QString, which is exactly what fleet-wide
    // means here (Document::isFleetWide()).
    document.vesselId  = query.value(1).toString();
    document.category  = categoryFromCode(query.value(2).toString());
    document.reference = query.value(3).toString();
    document.title     = query.value(4).toString();
    document.notes     = query.value(5).toString();
    return document;
}

} // namespace

DocumentRepository::DocumentRepository(QSqlDatabase&              database,
                                       const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
    , m_access(installation)
    , m_versions(database, installation.nodeId())
{
}

bool DocumentRepository::list(const QString& vesselId, QList<ListRow>* rows)
{
    m_errorString.clear();
    rows->clear();

    // The current version comes from a correlated lookup of the highest live
    // sequence rather than from a second round of queries: "current" is
    // computed, never stored (CLAUDE.md §6.7), and computing it once per row
    // in SQL keeps it that way without an N+1.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
                      "SELECT %1, %2,"
                      "       (SELECT COUNT(*) FROM document_version c"
                      "         WHERE c.document_id = d.id AND c.is_deleted = 0)"
                      "  FROM document d"
                      "  LEFT JOIN document_version v"
                      "         ON v.id = (SELECT w.id FROM document_version w"
                      "                     WHERE w.document_id = d.id AND w.is_deleted = 0"
                      "                     ORDER BY w.sequence DESC LIMIT 1)"
                      " WHERE d.is_deleted = 0"
                      "   AND (d.vessel_id IS NULL OR d.vessel_id = ?)"
                      // Documents with a reference first, in reference order,
                      // then the rest by title — the same judgment the
                      // certificate list makes about its own numbers.
                      " ORDER BY CASE WHEN d.reference IS NULL OR d.reference = ''"
                      "               THEN 1 ELSE 0 END,"
                      "          d.reference COLLATE NOCASE, d.title COLLATE NOCASE")
                      .arg(kDocumentColumns, DocumentVersionStore::columns()));
    query.addBindValue(m_access.scopedVesselId(vesselId));

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the documents:\n%1").arg(query.lastError().text());
        return false;
    }

    const int versionOffset = kDocumentColumnCount;
    const int countColumn   = kDocumentColumnCount + 8; // the version store names eight

    while (query.next()) {
        ListRow row;
        row.document = documentFromRow(query);

        // A document always has a version, since create() writes one — but a
        // row whose versions were all deleted would read back NULL here, and
        // an empty optional is the honest answer rather than a version with
        // an empty id.
        if (!query.value(versionOffset).isNull()) {
            row.currentVersion = DocumentVersionStore::fromRow(query, versionOffset);
        }
        row.versionCount = query.value(countColumn).toInt();

        rows->append(row);
    }
    return true;
}

bool DocumentRepository::findById(const QString& id, std::optional<Document>* document)
{
    m_errorString.clear();
    document->reset();

    // The scope applies here too, so asking for another vessel's document by
    // its id returns nothing rather than that document.
    //
    // "Is this installation scoped at all" is its own integer parameter
    // rather than a test on the scope string. An empty QString binds as SQL
    // NULL, and `NULL = ''` is unknown rather than true — so a "scope is
    // empty, see everything" clause written that way is false for every row,
    // and an OFFICE installation could not read its own vessels' documents.
    // That is not hypothetical: it is how this was first written, and what
    // the repository tests caught. VesselRepository carries a comment about
    // the same trap on its own excludeId.
    const bool    scoped = m_access.isScopedToOneVessel();
    const QString scope  = m_access.scopeForQueries();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document d"
                                 " WHERE d.id = ? AND d.is_deleted = 0"
                                 "   AND (? = 0 OR d.vessel_id IS NULL OR d.vessel_id = ?)")
                      .arg(kDocumentColumns));
    query.addBindValue(id);
    query.addBindValue(scoped ? 1 : 0);
    query.addBindValue(scope);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the document:\n%1").arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        *document = documentFromRow(query);
    }
    return true; // "not found" is a successful answer, not a failure
}

bool DocumentRepository::create(const Document& document, const DocumentVersion& firstVersion,
                                QString* newDocumentId)
{
    m_errorString.clear();

    if (!m_access.mayWrite(document.vesselId, &m_errorString)) {
        return false;
    }
    if (!m_access.validate(document, &m_errorString)) {
        return false;
    }
    if (firstVersion.filePath.isEmpty()) {
        m_errorString = QStringLiteral("A document needs a file, so nothing was saved.");
        return false;
    }

    // Both rows or neither: a document with no version has nothing to open,
    // and a version with no document is unreachable.
    TransactionScope transaction(m_database);
    if (!transaction.isOpen()) {
        m_errorString = QStringLiteral("The document could not be saved: no transaction.");
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO document"
        " (id, vessel_id, category, reference, title, notes,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    insert.addBindValue(id);
    // Fleet-wide is stored as NULL, which is what the nullable column means.
    insert.addBindValue(nullableText(document.vesselId));
    insert.addBindValue(categoryToCode(document.category));
    insert.addBindValue(nullableText(document.reference.trimmed()));
    insert.addBindValue(document.title.trimmed());
    insert.addBindValue(nullableText(document.notes.trimmed()));
    insert.addBindValue(now);
    insert.addBindValue(kSystemUser);
    insert.addBindValue(now);
    insert.addBindValue(kSystemUser);
    insert.addBindValue(m_installation.nodeId());

    if (!insert.exec()) {
        m_errorString =
            QStringLiteral("Could not save the document:\n%1").arg(insert.lastError().text());
        return false;
    }

    DocumentVersion version = firstVersion;
    version.sequence        = 1;
    if (!m_versions.insert(id, version, nullptr)) {
        m_errorString = m_versions.errorString();
        return false; // the scope rolls back
    }

    if (!transaction.commit()) {
        m_errorString =
            QStringLiteral("Could not save the document:\n%1").arg(m_database.lastError().text());
        return false;
    }

    if (newDocumentId != nullptr) {
        *newDocumentId = id;
    }
    return true;
}

bool DocumentRepository::addVersion(const QString& documentId, const DocumentVersion& version,
                                    QString* newVersionId)
{
    m_errorString.clear();

    if (documentId.isEmpty()) {
        m_errorString = QStringLiteral("No document was given, so no version was saved.");
        return false;
    }

    // Read the document first, through findById() — which applies the scope —
    // so a vessel cannot add a version to a document it may not even see.
    std::optional<Document> existing;
    if (!findById(documentId, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This document is no longer in the database.");
        return false;
    }
    if (!m_access.mayWrite(existing->vesselId, &m_errorString)) {
        return false;
    }

    if (!m_versions.insert(documentId, version, newVersionId)) {
        m_errorString = m_versions.errorString();
        return false;
    }
    return true;
}

bool DocumentRepository::listVersions(const QString& documentId, QList<DocumentVersion>* versions)
{
    m_errorString.clear();
    versions->clear();

    // Through findById() first, so another vessel's history is not readable
    // by asking for it directly.
    std::optional<Document> existing;
    if (!findById(documentId, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        return true; // not visible here: no versions, and not an error
    }

    if (!m_versions.listFor(documentId, versions)) {
        m_errorString = m_versions.errorString();
        return false;
    }
    return true;
}

bool DocumentRepository::currentVersion(const QString&                  documentId,
                                        std::optional<DocumentVersion>* version)
{
    version->reset();

    QList<DocumentVersion> versions;
    if (!listVersions(documentId, &versions)) {
        return false;
    }
    // Newest first, so the current one is simply the first.
    if (!versions.isEmpty()) {
        *version = versions.first();
    }
    return true;
}

bool DocumentRepository::updateDetails(const Document& document)
{
    m_errorString.clear();

    if (document.id.isEmpty()) {
        m_errorString = QStringLiteral("This document has no identifier, so it cannot be saved.");
        return false;
    }

    // The stored row decides what may be written, not the one handed in: a
    // caller that changed vesselId on its copy must not thereby gain the
    // right to write it.
    std::optional<Document> existing;
    if (!findById(document.id, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This document is no longer in the database.");
        return false;
    }
    if (!m_access.mayWrite(existing->vesselId, &m_errorString)) {
        return false;
    }
    if (!m_access.validate(document, &m_errorString)) {
        return false;
    }

    QSqlQuery query(m_database);
    // vessel_id is deliberately not in the SET list: changing a document's
    // scope would leave its files filed under the wrong folder, and moving
    // them is not in step 1 (§4.7).
    query.prepare(QStringLiteral("UPDATE document SET"
                                 "  category = ?, reference = ?, title = ?, notes = ?,"
                                 "  updated_at = ?, updated_by = ?, revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(categoryToCode(document.category));
    query.addBindValue(nullableText(document.reference.trimmed()));
    query.addBindValue(document.title.trimmed());
    query.addBindValue(nullableText(document.notes.trimmed()));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(document.id);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not save the document:\n%1").arg(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This document no longer exists and could not be saved.");
        return false;
    }
    return true;
}

bool DocumentRepository::softDelete(const QString& documentId)
{
    m_errorString.clear();

    std::optional<Document> existing;
    if (!findById(documentId, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This document is no longer in the database.");
        return false;
    }
    if (!m_access.mayWrite(existing->vesselId, &m_errorString)) {
        return false;
    }

    TransactionScope transaction(m_database);
    if (!transaction.isOpen()) {
        m_errorString = QStringLiteral("The document could not be deleted: no transaction.");
        return false;
    }

    const QString now = nowUtc();

    // The versions go with it, so a document's history does not reappear
    // under a document that is no longer there. The files on disk are left
    // exactly where they are (§4.4).
    if (!m_versions.retireAllFor(documentId, now)) {
        m_errorString = m_versions.errorString();
        return false;
    }

    QSqlQuery document(m_database);
    document.prepare(QStringLiteral("UPDATE document SET"
                                    "  is_deleted = 1, updated_at = ?, updated_by = ?,"
                                    "  revision = revision + 1"
                                    " WHERE id = ? AND is_deleted = 0"));
    document.addBindValue(now);
    document.addBindValue(kSystemUser);
    document.addBindValue(documentId);

    if (!document.exec()) {
        m_errorString =
            QStringLiteral("Could not delete the document:\n%1").arg(document.lastError().text());
        return false;
    }
    if (document.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This document no longer exists.");
        return false;
    }

    if (!transaction.commit()) {
        m_errorString = QStringLiteral("Could not delete the document:\n%1")
                            .arg(m_database.lastError().text());
        return false;
    }
    return true;
}

QString DocumentRepository::errorString() const
{
    return m_errorString;
}
