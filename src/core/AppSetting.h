#pragma once

#include <QDate>
#include <QString>

// The application's one settings row (settings-app-setting-spec §4).
//
// Core-owned, and deliberately free of any dependency on the certificates
// module: these are three plain numbers, and only the certificates module
// happens to be the one consumer today. The translation into that module's
// AlertThresholds happens on its side of the boundary, not here.
//
// No validation lives in the struct — that is the repository's job, the same
// convention Vessel and Certificate already follow. The audit columns are
// likewise absent for the same reason they are absent there: the repository
// owns them.
struct AppSetting {
    QString id;

    int criticalDays     = 30;
    int expiringSoonDays = 60;
    int dueSoonDays      = 90;

    // An invalid QDate means the daily toast has never been shown. Stored
    // from step 8, read and written from step 9.
    QDate lastAlertToastDate;
};
