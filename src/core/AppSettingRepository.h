#pragma once

#include "core/AppSetting.h"

#include <QString>

class QSqlDatabase;

// Reads and writes the single app_setting row
// (settings-app-setting-spec §5).
//
// No scope filtering and no InstallationContext: there is nothing vessel- or
// module-specific about these values, which is exactly why they are core.
class AppSettingRepository
{
public:
    explicit AppSettingRepository(QSqlDatabase& database);

    // Migration 006 seeds the row, so there is always exactly one. Returns
    // false with a message if it is somehow missing rather than assuming —
    // callers are expected to fall back to their own defaults instead of
    // failing outright.
    bool read(AppSetting* setting);

    // Validates before writing, bumps revision, and touches only
    // updated_at/updated_by — the same shape as every other update() here.
    // Deliberately never writes last_alert_toast_date.
    bool update(const AppSetting& setting);

    // Records that today's alert banner was shown (alerts-spec.md §5). A
    // separate, narrow method precisely because update() leaves that column
    // alone: saving thresholds must not disturb the toast date, and recording
    // the toast must not disturb the thresholds.
    bool recordAlertToastShown(const QDate& shownOn);

    // No create(): the row is seeded by the migration, never at runtime.

    QString errorString() const;

private:
    bool validate(const AppSetting& setting);

    QSqlDatabase& m_database;
    QString       m_errorString;
};
