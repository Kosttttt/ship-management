#include "core/VesselRepository.h"

#include "core/ImoNumberValidator.h"
#include "core/InstallationContext.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent set by migration records and the first-run wizard.
const QString kSystemUser = QStringLiteral("SYSTEM");

const QString kSelectColumns = QStringLiteral(
    "id, name, imo_number, call_sign, gross_tonnage, port_of_registry, flag_state");

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

// An optional field that was left blank is stored as NULL rather than as an
// empty string, so "not entered" and "deliberately blank" cannot drift apart
// later.
QVariant nullableText(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

QVariant nullableTonnage(int value)
{
    return value <= 0 ? QVariant(QMetaType(QMetaType::Int)) : QVariant(value);
}

Vessel vesselFromRow(const QSqlQuery& query)
{
    Vessel vessel;
    vessel.id             = query.value(0).toString();
    vessel.name           = query.value(1).toString();
    vessel.imoNumber      = query.value(2).toString();
    vessel.callSign       = query.value(3).toString();
    vessel.grossTonnage   = query.value(4).toInt(); // NULL reads back as 0
    vessel.portOfRegistry = query.value(5).toString();
    vessel.flagState      = query.value(6).toString();
    return vessel;
}

} // namespace

VesselRepository::VesselRepository(QSqlDatabase& database, const InstallationContext& installation)
    : m_database(database)
    , m_installation(installation)
{
}

bool VesselRepository::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

bool VesselRepository::list(QList<Vessel>* vessels)
{
    m_errorString.clear();
    vessels->clear();

    // is_deleted = 0 is here from day one even though nothing can set it yet:
    // a query that forgets it becomes a silent bug the day deactivation lands.
    QString sql = QStringLiteral("SELECT %1 FROM vessel WHERE is_deleted = 0").arg(kSelectColumns);

    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND id = ?");
    }
    sql += QStringLiteral(" ORDER BY name COLLATE NOCASE");

    QSqlQuery query(m_database);
    query.prepare(sql);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the vessel list:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    while (query.next()) {
        vessels->append(vesselFromRow(query));
    }
    return true;
}

bool VesselRepository::findById(const QString& id, std::optional<Vessel>* vessel)
{
    m_errorString.clear();
    vessel->reset();

    QString sql = QStringLiteral("SELECT %1 FROM vessel WHERE id = ? AND is_deleted = 0")
                      .arg(kSelectColumns);

    // The scope filter is applied here too, so asking for another vessel's id
    // by any route returns nothing rather than that vessel's data.
    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        sql += QStringLiteral(" AND id = ?");
    }

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(id);
    if (scoped) {
        query.addBindValue(m_installation.vesselScope());
    }

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not read the vessel:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.next()) {
        *vessel = vesselFromRow(query);
    }
    return true; // "not found" is a successful answer, not a failure
}

bool VesselRepository::validate(const Vessel& vessel)
{
    if (vessel.name.trimmed().isEmpty()) {
        m_errorString = QStringLiteral("A vessel name is required.");
        return false;
    }
    if (!ImoNumberValidator::isValid(vessel.imoNumber)) {
        m_errorString =
            QStringLiteral("\"%1\" is not a valid IMO number.").arg(vessel.imoNumber);
        return false;
    }
    if (vessel.grossTonnage < 0) {
        m_errorString = QStringLiteral("Gross tonnage cannot be negative.");
        return false;
    }
    return true;
}

bool VesselRepository::isImoNumberTaken(const QString& imoNumber,
                                        const QString& excludeId,
                                        bool*          taken)
{
    // The "exclude this row" clause is added only when there is a row to
    // exclude. Binding an empty QString would send SQL NULL, and `id != NULL`
    // is unknown rather than true, which silently matches nothing and lets
    // every duplicate through.
    QString    sql       = QStringLiteral(
        "SELECT 1 FROM vessel WHERE imo_number = ? AND is_deleted = 0");
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

    *taken = query.next();
    return true;
}

bool VesselRepository::create(const Vessel& vessel, QString* newId)
{
    m_errorString.clear();

    // Refused outright, not merely hidden in the UI: only OFFICE adds vessels.
    if (isScopedToOneVessel()) {
        m_errorString = QStringLiteral(
            "A vessel installation manages one ship and cannot add another.");
        return false;
    }

    if (!validate(vessel)) {
        return false;
    }

    const QString imoNumber = ImoNumberValidator::digitsOnly(vessel.imoNumber);

    bool taken = false;
    if (!isImoNumberTaken(imoNumber, QString(), &taken)) {
        return false;
    }
    if (taken) {
        // A friendly message rather than the raw SQLite constraint text. The
        // UNIQUE constraint stays as the backstop against a race.
        m_errorString = QStringLiteral("This IMO number already belongs to another vessel.");
        return false;
    }

    const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO vessel"
        " (id, name, imo_number, call_sign, gross_tonnage, port_of_registry, flag_state,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(vessel.name.trimmed());
    query.addBindValue(imoNumber);
    query.addBindValue(nullableText(vessel.callSign.trimmed()));
    query.addBindValue(nullableTonnage(vessel.grossTonnage));
    query.addBindValue(nullableText(vessel.portOfRegistry.trimmed()));
    query.addBindValue(nullableText(vessel.flagState.trimmed()));
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(now);
    query.addBindValue(kSystemUser);
    query.addBindValue(m_installation.nodeId());

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the vessel:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (newId != nullptr) {
        *newId = id;
    }
    return true;
}

bool VesselRepository::update(const Vessel& vessel)
{
    m_errorString.clear();

    if (vessel.id.isEmpty()) {
        m_errorString = QStringLiteral("This vessel has no identifier, so it cannot be saved.");
        return false;
    }

    // Even though the UI never offers another vessel's row to a VESSEL
    // installation, the repository refuses it rather than trusting that.
    if (isScopedToOneVessel() && vessel.id != m_installation.vesselScope()) {
        m_errorString = QStringLiteral("This installation may only edit its own vessel.");
        return false;
    }

    if (!validate(vessel)) {
        return false;
    }

    const QString imoNumber = ImoNumberValidator::digitsOnly(vessel.imoNumber);

    // Excluding the row's own id matters: without it, re-saving a vessel whose
    // IMO number was never touched would fail against itself.
    bool taken = false;
    if (!isImoNumberTaken(imoNumber, vessel.id, &taken)) {
        return false;
    }
    if (taken) {
        m_errorString = QStringLiteral("This IMO number already belongs to another vessel.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE vessel SET"
        "  name = ?, imo_number = ?, call_sign = ?, gross_tonnage = ?,"
        "  port_of_registry = ?, flag_state = ?,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(vessel.name.trimmed());
    query.addBindValue(imoNumber);
    query.addBindValue(nullableText(vessel.callSign.trimmed()));
    query.addBindValue(nullableTonnage(vessel.grossTonnage));
    query.addBindValue(nullableText(vessel.portOfRegistry.trimmed()));
    query.addBindValue(nullableText(vessel.flagState.trimmed()));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(vessel.id);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the vessel:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This vessel no longer exists and could not be saved.");
        return false;
    }

    return true;
}

QString VesselRepository::errorString() const
{
    return m_errorString;
}
