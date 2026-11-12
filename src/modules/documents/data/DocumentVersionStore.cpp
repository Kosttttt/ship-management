#include "modules/documents/data/DocumentVersionStore.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent every other repository here set.
const QString kSystemUser = QStringLiteral("SYSTEM");

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

} // namespace

DocumentVersionStore::DocumentVersionStore(QSqlDatabase& database, const QString& nodeId)
    : m_database(database)
    , m_nodeId(nodeId)
{
}

QString DocumentVersionStore::columns()
{
    return QStringLiteral(
        "v.id, v.document_id, v.sequence, v.revision_label, v.revision_date, v.change_note,"
        " v.file_path, v.original_file_name");
}

DocumentVersion DocumentVersionStore::fromRow(const QSqlQuery& query, int offset)
{
    DocumentVersion version;
    version.id            = query.value(offset + 0).toString();
    version.documentId    = query.value(offset + 1).toString();
    version.sequence      = query.value(offset + 2).toInt();
    version.revisionLabel = query.value(offset + 3).toString();
    // A calendar date, stored as YYYY-MM-DD text (CLAUDE.md §6.3). An absent
    // one reads back as an invalid QDate, which is how "not recorded" is
    // shown everywhere else in this project.
    version.revisionDate     = QDate::fromString(query.value(offset + 4).toString(), Qt::ISODate);
    version.changeNote       = query.value(offset + 5).toString();
    version.filePath         = query.value(offset + 6).toString();
    version.originalFileName = query.value(offset + 7).toString();
    return version;
}

bool DocumentVersionStore::nextSequence(const QString& documentId, int* sequence)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(sequence), 0) + 1 FROM document_version WHERE document_id = ?"));
    query.addBindValue(documentId);

    if (!query.exec() || !query.next()) {
        m_errorString = QStringLiteral("Could not work out the next version number:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    *sequence = query.value(0).toInt();
    return true;
}

bool DocumentVersionStore::insert(const QString& documentId, const DocumentVersion& version,
                                  QString* newId)
{
    m_errorString.clear();

    if (version.filePath.isEmpty()) {
        m_errorString = QStringLiteral("A version needs a file, so nothing was saved.");
        return false;
    }

    int sequence = version.sequence;
    if (sequence <= 0 && !nextSequence(documentId, &sequence)) {
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO document_version"
        " (id, document_id, sequence, revision_label, revision_date, change_note,"
        "  file_path, original_file_name,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(documentId);
    query.addBindValue(sequence);
    query.addBindValue(nullableText(version.revisionLabel.trimmed()));
    query.addBindValue(version.revisionDate.isValid()
                           ? QVariant(version.revisionDate.toString(Qt::ISODate))
                           : QVariant(QMetaType(QMetaType::QString)));
    query.addBindValue(nullableText(version.changeNote.trimmed()));
    query.addBindValue(version.filePath);
    query.addBindValue(version.originalFileName);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(m_nodeId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the version:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool DocumentVersionStore::listFor(const QString& documentId, QList<DocumentVersion>* versions)
{
    m_errorString.clear();
    versions->clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document_version v"
                                 " WHERE v.document_id = ? AND v.is_deleted = 0"
                                 " ORDER BY v.sequence DESC")
                      .arg(columns()));
    query.addBindValue(documentId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the versions:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        versions->append(fromRow(query, 0));
    }
    return true;
}

bool DocumentVersionStore::retireAllFor(const QString& documentId, const QString& now)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE document_version SET"
        "  is_deleted = 1, updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE document_id = ? AND is_deleted = 0"));
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(documentId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not delete the document's versions:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}

QString DocumentVersionStore::errorString() const
{
    return m_errorString;
}
