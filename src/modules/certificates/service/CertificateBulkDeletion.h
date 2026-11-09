#pragma once

#include <QList>
#include <QStringList>

struct Certificate;

class CertificateDeletionService;

// What a bulk delete actually managed to do
// (certificate-list-usability-fixes.md §2).
//
// Three outcomes rather than two, because CertificateDeletionService itself
// has three: the certificate went, the certificate did not go, or the
// certificate went but one of its files could not be removed from disk. That
// third case is a success the user still has to be told about - otherwise
// somebody finds the file in the attachment folder months later with no
// certificate left to explain it.
struct BulkDeletionOutcome
{
    // Names, for a message. Ids are not kept: nothing downstream needs them,
    // and the rows are gone by the time anyone reads this.
    QStringList deletedNames;

    // "name: reason", one per certificate that could not be deleted at all.
    QStringList failures;

    // "name: reason", one per certificate that was deleted while a file of
    // its own stayed behind.
    QStringList fileLeftovers;

    bool anythingDeleted() const { return !deletedNames.isEmpty(); }
};

// Deletes each certificate in turn, best effort, and reports on all of them.
//
// Deliberately **not** wrapped in an outer transaction. Two reasons, and both
// matter:
//
//  - It could not work. Each CertificateDeletionService::deleteCertificate()
//    call reaches CertificateRepository::softDeleteWithChildren(), which opens
//    and commits a transaction of its own, and SQLite has no nested
//    transactions - TransactionScope's own header says so.
//  - It should not work. Best effort per certificate is the behaviour that is
//    actually wanted here: if the seventh of twenty fails, the other nineteen
//    should still be gone rather than rolled back over one failure, and the
//    user told which one to look at. That is the opposite call from CSV
//    import (certificate-import-export-spec §5.2), where a half-written file
//    is worse than none - and the difference is that these twenty deletions
//    are twenty things the user asked for, not one.
//
// In the service layer rather than in the button that calls it, because
// "keep going after a failure, and report at the end" is a rule about what
// the operation means, not about how a dialog looks (CLAUDE.md §4 rule 3).
BulkDeletionOutcome deleteCertificates(CertificateDeletionService& deletion,
                                       const QList<Certificate>&   certificates);
