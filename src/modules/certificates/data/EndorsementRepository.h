#pragma once

#include "modules/certificates/domain/Endorsement.h"

#include <QDate>
#include <QList>
#include <QString>

class InstallationContext;
class QSqlDatabase;

// Reads and writes endorsements (certificate-endorsement-spec §7).
//
// Mirrors CertificateRepository, with one structural difference: an
// endorsement has no vessel_id of its own, so the VESSEL-mode scope filter is
// applied transitively, through the certificate the endorsement belongs to.
class EndorsementRepository
{
public:
    EndorsementRepository(QSqlDatabase& database, const InstallationContext& installation);

    // Joins the parent certificate so the scope filter can be applied there.
    // Another vessel's certificate returns nothing rather than an error — the
    // established "not found is a successful answer" precedent.
    bool list(const QString& certificateId, QList<Endorsement>* endorsements);

    // Looks the parent certificate up first, which both enforces scope and
    // supplies the issue date the "not before issue" check needs, in one
    // query. Refuses with a message rather than silently: this is a write.
    bool create(const Endorsement& endorsement, QString* newId = nullptr);

    // No update() or delete() this step. An endorsement is a compliance
    // record, and how a correction should work is a real design question
    // nothing here depends on (certificate-endorsement-spec §6).

    QString errorString() const;

private:
    // Fetches the parent's vessel id and issue date, applying scope.
    bool loadParentCertificate(const QString& certificateId,
                               QString*       vesselId,
                               QDate*         issueDate);
    bool validate(const Endorsement& endorsement, const QDate& certificateIssueDate);
    bool isScopedToOneVessel() const;

    QSqlDatabase&              m_database;
    const InstallationContext& m_installation;
    QString                    m_errorString;
};
