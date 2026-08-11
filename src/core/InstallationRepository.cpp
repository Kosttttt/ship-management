#include "core/InstallationRepository.h"

#include "core/ImoNumberValidator.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// No user system exists yet (CLAUDE.md §11), so rows created by the machine
// are attributed to SYSTEM — the precedent already set by migration records.
const QString kSystemUser = QStringLiteral("SYSTEM");

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString nowUtc()
{
    // CLAUDE.md §6.2: ISO-8601 UTC.
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString modeToText(InstallationMode mode)
{
    return (mode == InstallationMode::Vessel) ? QStringLiteral("VESSEL")
                                              : QStringLiteral("OFFICE");
}

} // namespace

InstallationRepository::InstallationRepository(QSqlDatabase& database)
    : m_database(database)
{
}

bool InstallationRepository::load(std::optional<InstallationRecord>* record)
{
    m_errorString.clear();
    record->reset();

    // LEFT JOIN: an OFFICE installation has no vessel, and must still load.
    QSqlQuery query(m_database);
    const bool ok = query.exec(QStringLiteral(
        "SELECT i.id, i.installation_mode, i.node_id, i.vessel_id, v.name, v.imo_number"
        "  FROM installation i"
        "  LEFT JOIN vessel v ON v.id = i.vessel_id"
        " WHERE i.is_deleted = 0"));

    if (!ok) {
        m_errorString = QStringLiteral("Could not read the installation row:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (!query.next()) {
        return true; // first run: no row yet, and that is not an error
    }

    InstallationRecord loaded;
    loaded.id   = query.value(0).toString();
    loaded.mode = (query.value(1).toString() == QLatin1String("VESSEL"))
                      ? InstallationMode::Vessel
                      : InstallationMode::Office;
    loaded.nodeId          = query.value(2).toString();
    loaded.vesselId        = query.value(3).toString();
    loaded.vesselName      = query.value(4).toString();
    loaded.vesselImoNumber = query.value(5).toString();

    *record = loaded;
    return true;
}

bool InstallationRepository::createOfficeInstallation(InstallationRecord* created)
{
    m_errorString.clear();

    InstallationRecord record;
    record.id     = newUuid();
    record.mode   = InstallationMode::Office;
    record.nodeId = QStringLiteral("OFFICE");
    // vesselId stays empty, stored as NULL — enforced by the CHECK in
    // migration 001.

    if (!writeInTransaction(record, /*withVessel=*/false)) {
        return false;
    }

    *created = record;
    return true;
}

bool InstallationRepository::createVesselInstallation(const QString& vesselName,
                                                      const QString& imoNumber,
                                                      InstallationRecord* created)
{
    m_errorString.clear();

    const QString name   = vesselName.trimmed();
    const QString digits = ImoNumberValidator::digitsOnly(imoNumber);

    // The wizard blocks Finish on invalid input, but the repository is also
    // reachable from tests and from step 4, so it checks for itself rather
    // than trusting a caller to have done it.
    if (name.isEmpty()) {
        m_errorString = QStringLiteral("A vessel name is required.");
        return false;
    }
    if (!ImoNumberValidator::isValid(digits)) {
        m_errorString = QStringLiteral("\"%1\" is not a valid IMO number.").arg(imoNumber);
        return false;
    }

    InstallationRecord record;
    record.id              = newUuid();
    record.mode            = InstallationMode::Vessel;
    record.nodeId          = QStringLiteral("VESSEL-") + digits;
    record.vesselId        = newUuid();
    record.vesselName      = name;
    record.vesselImoNumber = digits;

    if (!writeInTransaction(record, /*withVessel=*/true)) {
        return false;
    }

    *created = record;
    return true;
}

bool InstallationRepository::writeInTransaction(const InstallationRecord& record, bool withVessel)
{
    if (!m_database.transaction()) {
        m_errorString = QStringLiteral("Could not start a transaction:\n%1")
                            .arg(m_database.lastError().text());
        return false;
    }

    // The vessel row goes first so the installation row has something to point
    // at. If the second insert fails, the first is undone with it.
    if (withVessel && !insertVessel(record)) {
        m_database.rollback();
        return false;
    }

    if (!insertInstallation(record)) {
        m_database.rollback();
        return false;
    }

    if (!m_database.commit()) {
        m_errorString = QStringLiteral("Could not save the installation:\n%1")
                            .arg(m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    return true;
}

bool InstallationRepository::insertVessel(const InstallationRecord& record)
{
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO vessel"
        " (id, name, imo_number, created_at, created_by, updated_at, updated_by,"
        "  is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(record.vesselId);
    query.addBindValue(record.vesselName);
    query.addBindValue(record.vesselImoNumber);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(record.nodeId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the vessel:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}

bool InstallationRepository::insertInstallation(const InstallationRecord& record)
{
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO installation"
        " (id, singleton, installation_mode, node_id, vessel_id,"
        "  created_at, created_by, updated_at, updated_by,"
        "  is_deleted, origin_node, revision)"
        " VALUES (?, 1, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(record.id);
    query.addBindValue(modeToText(record.mode));
    query.addBindValue(record.nodeId);
    // An empty QString would be stored as '', which the CHECK in migration 001
    // treats as "not NULL" and rejects. A null QVariant stores a real NULL.
    query.addBindValue(record.vesselId.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                                 : QVariant(record.vesselId));
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(record.nodeId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the installation:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}

QString InstallationRepository::errorString() const
{
    return m_errorString;
}
