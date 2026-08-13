#pragma once

#include "modules/certificates/domain/Certificate.h"

#include <QList>
#include <QString>

#include <optional>

class InstallationContext;
class QSqlDatabase;

// The only file in this module containing SQL (CLAUDE.md §4 rule 2).
//
// Mirrors VesselRepository closely: the VESSEL-mode scope filter is applied
// here on every query, never left to a screen (CLAUDE.md §3).
class CertificateRepository
{
public:
    CertificateRepository(QSqlDatabase& database, const InstallationContext& installation);

    // Reads apply is_deleted = 0 and, in VESSEL mode, silently intersect the
    // requested vessel with the installation's own. Asking for another
    // vessel's data returns nothing rather than an error — "not found" is a
    // successful answer.
    bool list(const QString& vesselId, QList<Certificate>* certificates);
    bool findById(const QString& id, std::optional<Certificate>* certificate);

    // Writes refuse outright, with a message, when the certificate belongs to
    // another vessel under VESSEL mode. A silent empty result would be wrong
    // here: the user asked for something to be saved.
    bool create(const Certificate& certificate, QString* newId = nullptr);

    // Bumps revision and refreshes updated_at/updated_by, leaving created_at
    // and created_by alone (certificate-crud-spec §6).
    bool update(const Certificate& certificate);

    QString errorString() const;

private:
    bool validate(const Certificate& certificate);
    bool checkVesselAllowed(const QString& vesselId);
    bool isScopedToOneVessel() const;

    QSqlDatabase&              m_database;
    const InstallationContext& m_installation;
    QString                    m_errorString;
};
