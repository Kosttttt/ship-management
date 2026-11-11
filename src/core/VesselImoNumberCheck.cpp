#include "core/VesselImoNumberCheck.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

VesselImoNumberCheck::VesselImoNumberCheck(QSqlDatabase& database)
    : m_database(database)
{
}

bool VesselImoNumberCheck::findHolder(const QString& imoNumber, const QString& excludeId,
                                      std::optional<Holder>* holder)
{
    m_errorString.clear();
    holder->reset();

    // No is_deleted filter, on purpose — see the note in the header. An
    // archived vessel keeps its IMO number, exactly as the UNIQUE constraint
    // in migrations/002 insists, and in the same way the real register does:
    // a number belongs to a hull for life and is never reissued.
    //
    // The "exclude this row" clause is added only when there is a row to
    // exclude. Binding an empty QString would send SQL NULL, and `id != NULL`
    // is unknown rather than true, which silently matches nothing and lets
    // every duplicate through.
    QString    sql       = QStringLiteral(
        "SELECT name, is_deleted FROM vessel WHERE imo_number = ?");
    const bool excluding = !excludeId.isEmpty();
    if (excluding) {
        sql += QStringLiteral(" AND id != ?");
    }

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(imoNumber);
    if (excluding) {
        query.addBindValue(excludeId);
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not check the IMO number:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        Holder found;
        found.name       = query.value(0).toString();
        found.isArchived = query.value(1).toInt() != 0;
        *holder          = found;
    }
    return true;
}

QString VesselImoNumberCheck::conflictMessage(const Holder& holder, bool suggestRestore)
{
    if (!holder.isArchived) {
        // Unchanged from before archiving existed: naming an active vessel
        // would be telling the user about a ship they can already see.
        return QStringLiteral("This IMO number already belongs to another vessel.");
    }

    // An archived holder is invisible by default, so a generic message would
    // send someone hunting for a vessel that is not in the list. Name it.
    if (!suggestRestore) {
        return QStringLiteral("This IMO number belongs to the archived vessel '%1'.")
            .arg(holder.name);
    }
    return QStringLiteral("This IMO number belongs to the archived vessel '%1'. "
                          "Tick \"Show archived vessels\" and restore it instead of "
                          "adding it again.")
        .arg(holder.name);
}

QString VesselImoNumberCheck::errorString() const
{
    return m_errorString;
}
