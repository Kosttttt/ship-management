#pragma once

#include "core/InstallationContext.h"

#include <QString>

#include <optional>

class QSqlDatabase;

// Reads and writes the installation identity. CLAUDE.md §4 rule 2: this is the
// only file in step 3 that contains SQL.
class InstallationRepository
{
public:
    explicit InstallationRepository(QSqlDatabase& database);

    // Loads the single installation row, if there is one. An empty optional
    // means first run. Returns false only on a database error — "no row" is a
    // successful answer, not a failure.
    bool load(std::optional<InstallationRecord>* record);

    // Both create methods write everything in one transaction
    // (first-run-wizard-spec §4): on any failure nothing at all is written,
    // so a half-configured installation cannot exist.
    bool createOfficeInstallation(InstallationRecord* created);
    bool createVesselInstallation(const QString& vesselName,
                                  const QString& imoNumber,
                                  InstallationRecord* created);

    QString errorString() const;

private:
    bool insertVessel(const InstallationRecord& record);
    bool insertInstallation(const InstallationRecord& record);
    bool writeInTransaction(const InstallationRecord& record, bool withVessel);

    QSqlDatabase& m_database;
    QString       m_errorString;
};
