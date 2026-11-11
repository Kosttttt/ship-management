#include "core/VesselArchiveWriter.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

// The same attribution the rest of the repository layer uses: no user system
// exists yet (CLAUDE.md §11).
const QString kSystemUser = QStringLiteral("SYSTEM");

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

} // namespace

VesselArchiveWriter::VesselArchiveWriter(QSqlDatabase&              database,
                                        const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
{
}

bool VesselArchiveWriter::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool VesselArchiveWriter::checkChangeAllowed(const QString& vesselId, bool wantArchived,
                                             QString* imoNumber)
{
    m_errorString.clear();
    imoNumber->clear();

    if (vesselId.isEmpty()) {
        m_errorString = wantArchived
                            ? QStringLiteral("No vessel was given, so nothing was archived.")
                            : QStringLiteral("No vessel was given, so nothing was restored.");
        return false;
    }

    // The bare check create() uses, not update()'s "scoped and not my own id"
    // form: a VESSEL installation may not archive any vessel, its own
    // included. This is fleet management (vessel-archive-spec §3).
    if (isScopedToOneVessel()) {
        m_errorString =
            QStringLiteral("A vessel installation cannot archive or restore a vessel.");
        return false;
    }

    // Read the row before writing, archived rows included, so that "there is
    // no such vessel" and "it is already in that state" can be told apart and
    // said honestly. numRowsAffected() cannot distinguish them — both come
    // back zero — which is why update()'s single combined message is not
    // reused here (vessel-archive-spec §6).
    //
    // No vessel-scope clause, and none is missing: the refusal above means
    // this line is only ever reached by an OFFICE installation, whose scope
    // filter is empty anyway. The read this replaced went through
    // VesselRepository::findById(), which added that clause only when scoped.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT imo_number, is_deleted FROM vessel WHERE id = ?"));
    query.addBindValue(vesselId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the vessel:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (!query.next()) {
        m_errorString = QStringLiteral("This vessel is no longer in the database.");
        return false;
    }

    const bool isArchived = query.value(1).toInt() != 0;
    if (isArchived == wantArchived) {
        m_errorString = wantArchived ? QStringLiteral("This vessel is already archived.")
                                     : QStringLiteral("This vessel is not archived.");
        return false;
    }

    *imoNumber = query.value(0).toString();
    return true;
}

bool VesselArchiveWriter::writeFlag(const QString& vesselId, bool archived)
{
    m_errorString.clear();

    // The WHERE still names the flag's previous value, so a row that changed
    // underneath this call between the pre-read and here matches nothing
    // rather than being written twice.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE vessel SET"
        "  is_deleted = ?,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = ?"));
    query.addBindValue(archived ? 1 : 0);
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(vesselId);
    query.addBindValue(archived ? 0 : 1);

    if (!query.exec()) {
        m_errorString = (archived ? QStringLiteral("Could not archive the vessel:\n%1")
                                  : QStringLiteral("Could not restore the vessel:\n%1"))
                            .arg(query.lastError().text());
        return false;
    }

    // The pre-read already ruled out both reasons this could be zero. Kept as
    // the backstop for the gap between the two statements rather than as the
    // primary check.
    if (query.numRowsAffected() == 0) {
        m_errorString = archived ? QStringLiteral("This vessel could not be archived.")
                                 : QStringLiteral("This vessel could not be restored.");
        return false;
    }

    return true;
}

QString VesselArchiveWriter::errorString() const
{
    return m_errorString;
}
