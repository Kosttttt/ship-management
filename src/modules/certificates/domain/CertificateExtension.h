#pragma once

#include <QDate>
#include <QString>

// The domain layer: plain C++ with no widgets and no SQL (CLAUDE.md §4 rule 1).

// A short-term certificate bridging a problem with the real certificate's
// cycle (certificate-extension-spec §4).
//
// Deliberately the same shape as Endorsement: a plain struct with no .cpp, no
// behaviour of its own, and no enum needing stored codes. A certificate has at
// most one of these active at a time; resolving it soft-deletes the row, so
// "active" and "not deleted" are the same fact.
struct CertificateExtension
{
    QString id;
    QString certificateId;

    // Required. A calendar date, never an instant. There is no "does not
    // expire" case: a short-term certificate that never expires is not a
    // meaningful concept (§4).
    QDate expiryDate;

    QString note; // optional; may be empty
};
