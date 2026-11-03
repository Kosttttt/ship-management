#pragma once

#include "core/InstallationContext.h"
#include "modules/certificates/domain/Endorsement.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

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

    // One endorsement by its own id, for the edit dialog to pre-fill from.
    // Scoped the same transitive way list() is.
    bool findById(const QString& id, std::optional<Endorsement>* endorsement);

    // Looks the parent certificate up first, which both enforces scope and
    // supplies the issue date the "not before issue" check needs, in one
    // query. Refuses with a message rather than silently: this is a write.
    bool create(const Endorsement& endorsement, QString* newId = nullptr);

    // Saves corrections back onto the same row
    // (certificate-attachment-addendum-2-spec §6) — an ordinary UPDATE, never
    // a second row. Editing an endorsement is fixing a typo in a record of
    // something that happened once; a new row would claim a second survey.
    //
    // Validated exactly as create() is, including "not before the certificate
    // was issued", because a correction can be wrong in all the same ways an
    // entry can. Bumps revision and refreshes updated_at/updated_by, leaving
    // created_at and created_by alone.
    bool update(const Endorsement& endorsement);

    // Soft-deletes one endorsement (§6). It then disappears from list(), and
    // so from the dialog's table and from computeCertificateState()'s
    // anniversary matching, the same as every other soft-deleted row in the
    // app.
    bool softDelete(const QString& id);

    // Soft-deletes every endorsement recorded against one certificate, used
    // when it is renewed: the completed cycle's surveys do not describe the
    // new one (certificate-renewal-spec §4). Affecting zero rows is success,
    // not failure — a certificate with nothing recorded is a normal case.
    bool deleteAllForCertificate(const QString& certificateId);

    QString errorString() const;

private:
    // Fetches the parent's vessel id and issue date, applying scope.
    bool loadParentCertificate(const QString& certificateId,
                               QString*       vesselId,
                               QDate*         issueDate);

    // The parent of one endorsement, found by the endorsement's own id, so a
    // write addressed by that id can be scope-checked too. An endorsement
    // carries no vessel of its own, so this is the only way to ask.
    bool loadParentOfEndorsement(const QString& endorsementId,
                                 QString*       certificateId,
                                 QDate*         issueDate);
    bool validate(const Endorsement& endorsement, const QDate& certificateIssueDate);
    bool isScopedToOneVessel() const;

    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    QString             m_errorString;
};
