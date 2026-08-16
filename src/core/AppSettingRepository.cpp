#include "core/AppSettingRepository.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent every repository here already follows.
const QString kSystemUser = QStringLiteral("SYSTEM");

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

} // namespace

AppSettingRepository::AppSettingRepository(QSqlDatabase& database)
    : m_database(database)
{
}

bool AppSettingRepository::read(AppSetting* setting)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    const bool ok = query.exec(QStringLiteral(
        "SELECT id, critical_days, expiring_soon_days, due_soon_days, last_alert_toast_date"
        "  FROM app_setting WHERE is_deleted = 0"));

    if (!ok) {
        m_errorString = QStringLiteral("Could not read the application settings:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (!query.next()) {
        // Migration 006 seeds this row, so its absence means something is
        // genuinely wrong. Say so rather than silently inventing values.
        m_errorString = QStringLiteral("The application settings row is missing.");
        return false;
    }

    setting->id               = query.value(0).toString();
    setting->criticalDays     = query.value(1).toInt();
    setting->expiringSoonDays = query.value(2).toInt();
    setting->dueSoonDays      = query.value(3).toInt();
    // A NULL toast date reads back as an invalid QDate: never shown.
    setting->lastAlertToastDate =
        query.value(4).isNull() ? QDate()
                                : QDate::fromString(query.value(4).toString(), Qt::ISODate);
    return true;
}

bool AppSettingRepository::validate(const AppSetting& setting)
{
    // Each threshold is a number of days, so zero or negative is meaningless.
    struct NamedValue {
        const char* label;
        int         value;
    };
    const NamedValue values[] = {{"Critical", setting.criticalDays},
                                 {"Expiring Soon", setting.expiringSoonDays},
                                 {"Due Soon", setting.dueSoonDays}};

    for (const NamedValue& named : values) {
        if (named.value < 1) {
            m_errorString = QStringLiteral("%1 must be at least 1 day, but is %2.")
                                .arg(QLatin1String(named.label))
                                .arg(named.value);
            return false;
        }
    }

    // Strictly increasing. This is correctness, not tidiness:
    // computeCertificateState() tests critical, then expiring-soon, then
    // due-soon in that order, so an out-of-order set makes a whole severity
    // tier unreachable. The message names the offending pair rather than
    // saying "invalid" and leaving the user to work out which.
    if (setting.criticalDays >= setting.expiringSoonDays) {
        m_errorString = QStringLiteral(
            "Critical (%1 days) must be fewer than Expiring Soon (%2 days).")
                            .arg(setting.criticalDays)
                            .arg(setting.expiringSoonDays);
        return false;
    }
    if (setting.expiringSoonDays >= setting.dueSoonDays) {
        m_errorString = QStringLiteral(
            "Expiring Soon (%1 days) must be fewer than Due Soon (%2 days).")
                            .arg(setting.expiringSoonDays)
                            .arg(setting.dueSoonDays);
        return false;
    }

    return true;
}

bool AppSettingRepository::update(const AppSetting& setting)
{
    m_errorString.clear();

    if (!validate(setting)) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE app_setting SET"
        "  critical_days = ?, expiring_soon_days = ?, due_soon_days = ?,"
        "  updated_at = ?, updated_by = ?, revision = revision + 1"
        " WHERE is_deleted = 0"));
    query.addBindValue(setting.criticalDays);
    query.addBindValue(setting.expiringSoonDays);
    query.addBindValue(setting.dueSoonDays);
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);

    // last_alert_toast_date is deliberately not written here: nothing owns it
    // until step 9, and an update from this screen must not clear it.

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not save the application settings:\n%1")
                            .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() == 0) {
        m_errorString = QStringLiteral("The application settings row is missing.");
        return false;
    }

    return true;
}

bool AppSettingRepository::recordAlertToastShown(const QDate& shownOn)
{
    m_errorString.clear();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE app_setting SET"
        "  last_alert_toast_date = ?, updated_at = ?, updated_by = ?,"
        "  revision = revision + 1"
        " WHERE is_deleted = 0"));
    query.addBindValue(shownOn.toString(Qt::ISODate));
    query.addBindValue(nowUtc());
    query.addBindValue(kSystemUser);

    if (!query.exec()) {
        m_errorString = QStringLiteral("Could not record that the alert banner was shown:\n%1")
                            .arg(query.lastError().text());
        return false;
    }
    return true;
}

QString AppSettingRepository::errorString() const
{
    return m_errorString;
}
