#include "modules/documents/data/DocumentVesselSpaces.h"

#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentSql.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

DocumentVesselSpaces::DocumentVesselSpaces(QSqlDatabase&              database,
                                           const InstallationContext& installation,
                                           VesselRepository&          vessels)
    : m_database(database)
    , m_installation(installation)
    , m_vessels(vessels)
{
}

bool DocumentVesselSpaces::ensureAll(int* createdCount)
{
    m_errorString.clear();
    if (createdCount != nullptr) {
        *createdCount = 0;
    }

    QList<Vessel> vessels;
    if (!m_vessels.list(&vessels)) { // active vessels only
        m_errorString = m_vessels.errorString();
        return false;
    }

    for (const Vessel& vessel : vessels) {
        // Any row at all, **deleted ones included**: a space that was
        // removed by hand must not come back the next time this screen is
        // opened, and "running it twice creates no duplicates" has to hold
        // whatever state the table is in.
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM document_space WHERE vessel_id = ?"));
        existing.addBindValue(vessel.id);

        if (!existing.exec() || !existing.next()) {
            m_errorString = QStringLiteral("Could not check this vessel's space:\n%1")
                                .arg(existing.lastError().text());
            return false;
        }
        if (existing.value(0).toInt() > 0) {
            continue;
        }

        const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString now = DocumentSql::nowUtc();

        QSqlQuery insert(m_database);
        // Always a Collection (§2): a vessel's space is its own circulars,
        // reports and notes, not a controlled manual with revisions of the
        // whole.
        insert.prepare(QStringLiteral(
            "INSERT INTO document_space"
            " (id, name, kind, vessel_id, description, position,"
            "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
            " VALUES (?, ?, 'COLLECTION', ?, NULL, 0, ?, ?, ?, ?, 0, ?, 1)"));
        insert.addBindValue(id);
        insert.addBindValue(vessel.name);
        insert.addBindValue(vessel.id);
        insert.addBindValue(now);
        insert.addBindValue(DocumentSql::systemUser());
        insert.addBindValue(now);
        insert.addBindValue(DocumentSql::systemUser());
        insert.addBindValue(m_installation.nodeId());

        if (!insert.exec()) {
            m_errorString = QStringLiteral("Could not create the space for %1:\n%2")
                                .arg(vessel.name, insert.lastError().text());
            return false;
        }
        if (createdCount != nullptr) {
            *createdCount += 1;
        }
    }
    return true;
}

QString DocumentVesselSpaces::errorString() const
{
    return m_errorString;
}
