#include "modules/documents/data/DocumentSpaceRepository.h"

#include "core/Vessel.h"
#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace {

// Positional, and read back positionally in spaceFromRow(). Anything new goes
// on the END of this list, never into the middle: inserting a column anywhere
// else shifts every field after it by one, and nothing about that mistake
// looks wrong at a glance.
const QString kColumns =
    QStringLiteral("id, name, kind, vessel_id, description, position");

DocumentSpace spaceFromRow(const QSqlQuery& query)
{
    DocumentSpace space;
    space.id   = query.value(0).toString();
    space.name = query.value(1).toString();
    space.kind = kindFromCode(query.value(2).toString());
    // NULL reads back as an empty QString, which is what "company space"
    // means here (DocumentSpace::isVesselSpace()).
    space.vesselId    = query.value(3).toString();
    space.description = query.value(4).toString();
    space.position    = query.value(5).toInt();
    return space;
}

} // namespace

DocumentSpaceRepository::DocumentSpaceRepository(QSqlDatabase&              database,
                                                 const InstallationContext& installation,
                                                 VesselRepository&          vessels)
    : m_database(database)
    , m_installation(installation)
    , m_vessels(vessels)
{
}

bool DocumentSpaceRepository::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool DocumentSpaceRepository::visibleVesselIds(QStringList* ids)
{
    ids->clear();

    QList<Vessel> vessels;
    if (!m_vessels.list(&vessels)) {
        m_errorString = m_vessels.errorString();
        return false;
    }
    for (const Vessel& vessel : vessels) {
        ids->append(vessel.id);
    }
    return true;
}

bool DocumentSpaceRepository::list(QList<DocumentSpace>* spaces)
{
    m_errorString.clear();
    spaces->clear();

    QStringList visible;
    if (!visibleVesselIds(&visible)) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM document_space"
                                 " WHERE is_deleted = 0"
                                 // Company spaces first (§5: the office's
                                 // spaces, then the ships'), each group by the
                                 // position the office arranged and then by
                                 // name so the order is never arbitrary.
                                 " ORDER BY CASE WHEN vessel_id IS NULL THEN 0 ELSE 1 END,"
                                 "          position, name COLLATE NOCASE")
                      .arg(kColumns));

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not read the spaces:\n%1").arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        const DocumentSpace space = spaceFromRow(query);

        // A vessel space is shown only when its vessel is one this
        // installation can see. That covers both of §4.3's rules at once: a
        // VESSEL installation sees only its own vessel, and an archived
        // vessel is not in the list at all — so its space is hidden rather
        // than deleted, which is what the spec asks for.
        if (space.isVesselSpace() && !visible.contains(space.vesselId)) {
            continue;
        }
        spaces->append(space);
    }
    return true;
}

bool DocumentSpaceRepository::findById(const QString& id, std::optional<DocumentSpace>* space)
{
    m_errorString.clear();
    space->reset();

    // Through list(), not a query of its own: the visibility rule then has
    // exactly one implementation, and asking for another vessel's space by
    // its id cannot slip past it.
    QList<DocumentSpace> spaces;
    if (!list(&spaces)) {
        return false;
    }
    for (const DocumentSpace& candidate : spaces) {
        if (candidate.id == id) {
            *space = candidate;
            return true;
        }
    }
    return true; // not visible here: a successful "no", not a failure
}

bool DocumentSpaceRepository::mayEdit(const DocumentSpace& space) const
{
    if (!isScopedToOneVessel()) {
        return true; // the office edits everything
    }
    // On a ship: its own space only. Company spaces are read-only there.
    return space.isVesselSpace() && space.vesselId == m_installation.vesselScope();
}

bool DocumentSpaceRepository::createCompanySpace(const QString& name, SpaceKind kind,
                                                 const QString& description, QString* newId)
{
    m_errorString.clear();

    if (isScopedToOneVessel()) {
        m_errorString = QStringLiteral(
            "Company spaces are created at the office. This computer keeps its own vessel's "
            "space only.");
        return false;
    }
    if (name.trimmed().isEmpty()) {
        m_errorString = QStringLiteral("A space needs a name.");
        return false;
    }
    if (kindToCode(kind).isEmpty()) {
        m_errorString = QStringLiteral("Choose whether this is a manual or a collection.");
        return false;
    }

    // One past the last company space, so a new one lands at the end of the
    // list rather than wherever a default would put it.
    int      position = 0;
    QSqlQuery next(m_database);
    if (next.exec(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM document_space"
                                 " WHERE vessel_id IS NULL"))
        && next.next()) {
        position = next.value(0).toInt();
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = DocumentSql::nowUtc();

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO document_space"
        " (id, name, kind, vessel_id, description, position,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, NULL, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    insert.addBindValue(id);
    insert.addBindValue(name.trimmed());
    insert.addBindValue(kindToCode(kind));
    insert.addBindValue(DocumentSql::nullableText(description.trimmed()));
    insert.addBindValue(position);
    insert.addBindValue(now);
    insert.addBindValue(DocumentSql::systemUser());
    insert.addBindValue(now);
    insert.addBindValue(DocumentSql::systemUser());
    insert.addBindValue(m_installation.nodeId());

    if (!insert.exec()) {
        m_errorString =
            QStringLiteral("Could not create the space:\n%1").arg(insert.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool DocumentSpaceRepository::rename(const QString& spaceId, const QString& name)
{
    m_errorString.clear();

    if (name.trimmed().isEmpty()) {
        m_errorString = QStringLiteral("A space needs a name.");
        return false;
    }

    std::optional<DocumentSpace> existing;
    if (!findById(spaceId, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This space is no longer there.");
        return false;
    }
    if (!mayEdit(*existing)) {
        m_errorString = QStringLiteral(
            "Company spaces are managed at the office and cannot be changed here.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE document_space SET"
                                 "  name = ?, updated_at = ?, updated_by = ?,"
                                 "  revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(name.trimmed());
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(spaceId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not rename the space:\n%1").arg(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DocumentSpaceRepository::softDelete(const QString& spaceId)
{
    m_errorString.clear();

    std::optional<DocumentSpace> existing;
    if (!findById(spaceId, &existing)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This space is no longer there.");
        return false;
    }
    if (existing->isVesselSpace()) {
        // It belongs to the vessel rather than to whoever is looking at it,
        // and §4.3 says an archived vessel's space is hidden, not removed.
        m_errorString = QStringLiteral(
            "A vessel's own space cannot be deleted. It disappears by itself when the vessel "
            "is archived.");
        return false;
    }
    if (!mayEdit(*existing)) {
        m_errorString = QStringLiteral(
            "Company spaces are managed at the office and cannot be deleted here.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE document_space SET"
                                 "  is_deleted = 1, updated_at = ?, updated_by = ?,"
                                 "  revision = revision + 1"
                                 " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(DocumentSql::nowUtc());
    query.addBindValue(DocumentSql::systemUser());
    query.addBindValue(spaceId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not delete the space:\n%1").arg(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

QString DocumentSpaceRepository::errorString() const
{
    return m_errorString;
}
