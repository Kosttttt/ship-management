#include "core/VesselRepository.h"

#include "core/ImoNumberValidator.h"
#include "core/InstallationContext.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent set by migration records and the first-run wizard.
const QString kSystemUser = QStringLiteral("SYSTEM");

// Positional, and read back positionally in vesselFromRow(). Anything new
// goes on the END of this list, never into the middle of it: inserting a
// column anywhere else shifts every field after it by one, and nothing about
// that mistake looks wrong at a glance — each field simply starts reading the
// column to its right (vessel-archive-spec §6).
const QString kSelectColumns = QStringLiteral(
    "id, name, imo_number, call_sign, gross_tonnage, port_of_registry, flag_state,"
    " attachment_root_path, is_deleted");

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
    // NULL reads back as an empty string, which is exactly what "not
    // configured yet" means (certificate-attachment-addendum-spec §2.3).
    vessel.attachmentRootPath = query.value(7).toString();
    // Appended last, so every index above keeps the column it always had.
    vessel.isArchived = query.value(8).toInt() != 0;
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

bool VesselRepository::list(QList<Vessel>* vessels, bool includeArchived)
{
    m_errorString.clear();
    vessels->clear();

    // The day that comment warned about arrived: archive() sets is_deleted
    // now. The filter stays the default, so every caller written before this
    // one keeps the fleet it always had; only a caller that deliberately asks
    // sees archived vessels (vessel-archive-spec §6).
    // Built from whichever filters actually apply, rather than from a
    // "WHERE 1 = 1" that exists only to let the next line start with AND.
    QStringList conditions;
    if (!includeArchived) {
        conditions << QStringLiteral("is_deleted = 0");
    }

    // The scope filter is never optional (CLAUDE.md §3).
    const bool scoped = isScopedToOneVessel();
    if (scoped) {
        conditions << QStringLiteral("id = ?");
    }

    QString sql = QStringLiteral("SELECT %1 FROM vessel").arg(kSelectColumns);
    if (!conditions.isEmpty()) {
        sql += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
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

bool VesselRepository::findById(const QString& id, std::optional<Vessel>* vessel,
                                bool includeArchived)
{
    m_errorString.clear();
    vessel->reset();

    QString sql =
        QStringLiteral("SELECT %1 FROM vessel WHERE id = ?").arg(kSelectColumns);
    if (!includeArchived) {
        sql += QStringLiteral(" AND is_deleted = 0");
    }

    // The scope filter is applied here too, so asking for another vessel's id
    // by any route returns nothing rather than that vessel's data. This is
    // not relaxed by includeArchived: seeing archived rows is a different
    // question from seeing another ship's rows.
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
        "  attachment_root_path,"
        "  created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, 1)"));
    query.addBindValue(id);
    query.addBindValue(vessel.name.trimmed());
    query.addBindValue(imoNumber);
    query.addBindValue(nullableText(vessel.callSign.trimmed()));
    query.addBindValue(nullableTonnage(vessel.grossTonnage));
    query.addBindValue(nullableText(vessel.portOfRegistry.trimmed()));
    query.addBindValue(nullableText(vessel.flagState.trimmed()));
    // Normally empty: a vessel being added has no folder yet, and Settings is
    // where one is chosen. Carried through rather than hardcoded to NULL so
    // that a caller which does know the folder is not silently ignored.
    query.addBindValue(nullableText(vessel.attachmentRootPath.trimmed()));
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

bool VesselRepository::setAttachmentRootPath(const QString& vesselId,
                                             const QString& absolutePath)
{
    m_errorString.clear();

    if (vesselId.isEmpty()) {
        m_errorString = QStringLiteral("No vessel was given, so no folder could be stored.");
        return false;
    }

    // The same refusal update() makes, for the same reason: the screen never
    // offers another vessel's row to a VESSEL installation, and the repository
    // does not take that on trust (CLAUDE.md §3).
    if (isScopedToOneVessel() && vesselId != m_installation.vesselScope()) {
        m_errorString = QStringLiteral("This installation may only change its own vessel.");
        return false;
    }

    // No validate() call here: this touches one column and leaves every field
    // validate() checks exactly as it found it. Running it would mean a vessel
    // carrying, say, a legacy IMO number could never be given a folder.
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE vessel SET"
        "  attachment_root_path = ?,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(nullableText(absolutePath.trimmed()));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(vesselId);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not store the attachment folder:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This vessel no longer exists, so no folder was stored.");
        return false;
    }

    return true;
}

bool VesselRepository::archive(const QString& vesselId)
{
    m_errorString.clear();

    if (vesselId.isEmpty()) {
        m_errorString = QStringLiteral("No vessel was given, so nothing was archived.");
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

    // Read the row before writing, including archived rows, so that "there is
    // no such vessel" and "it is already archived" can be told apart and said
    // honestly. numRowsAffected() cannot distinguish them — both come back
    // zero — which is why update()'s single combined message is not reused
    // here (vessel-archive-spec §6).
    std::optional<Vessel> existing;
    if (!findById(vesselId, &existing, /*includeArchived=*/true)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This vessel is no longer in the database.");
        return false;
    }
    if (existing->isArchived) {
        m_errorString = QStringLiteral("This vessel is already archived.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE vessel SET"
        "  is_deleted = 1,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 0"));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(vesselId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not archive the vessel:\n%1").arg(query.lastError().text());
        return false;
    }

    // The pre-read above already ruled out both reasons this could be zero.
    // Kept as the backstop for the gap between the two statements rather than
    // as the primary check.
    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This vessel could not be archived.");
        return false;
    }

    return true;
}

bool VesselRepository::restore(const QString& vesselId)
{
    m_errorString.clear();

    if (vesselId.isEmpty()) {
        m_errorString = QStringLiteral("No vessel was given, so nothing was restored.");
        return false;
    }

    if (isScopedToOneVessel()) {
        m_errorString =
            QStringLiteral("A vessel installation cannot archive or restore a vessel.");
        return false;
    }

    std::optional<Vessel> existing;
    if (!findById(vesselId, &existing, /*includeArchived=*/true)) {
        return false;
    }
    if (!existing.has_value()) {
        m_errorString = QStringLiteral("This vessel is no longer in the database.");
        return false;
    }
    if (!existing->isArchived) {
        m_errorString = QStringLiteral("This vessel is not archived.");
        return false;
    }

    // Kept as a backstop, though it cannot currently fire: migrations/002
    // declares imo_number UNIQUE with no is_deleted condition, so nothing can
    // claim this number while the vessel sits archived, and the restore can
    // never lose a race for it. vessel-archive-spec §6 predicts the opposite
    // — see anArchivedVesselStillHoldsItsImoNumber() in
    // tst_VesselRepositoryWrites for why it does not hold.
    //
    // Left in rather than removed: it costs one query, it is the same check
    // create() and update() make, and if that UNIQUE is ever relaxed to a
    // partial index this becomes load-bearing on the same day.
    bool taken = false;
    if (!isImoNumberTaken(existing->imoNumber, vesselId, &taken)) {
        return false;
    }
    if (taken) {
        m_errorString = QStringLiteral("This IMO number already belongs to another vessel.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE vessel SET"
        "  is_deleted = 0,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE id = ? AND is_deleted = 1"));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);
    query.addBindValue(vesselId);

    if (!query.exec()) {
        m_errorString =
            QStringLiteral("Could not restore the vessel:\n%1").arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("This vessel could not be restored.");
        return false;
    }

    return true;
}

QString VesselRepository::errorString() const
{
    return m_errorString;
}
