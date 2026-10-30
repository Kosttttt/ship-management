#pragma once

#include <QString>

// The domain layer: plain C++ with no widgets and no SQL (CLAUDE.md §4 rule 1).

// The real document behind a certificate — a scan, a PDF, or a whole folder of
// files (certificate-attachment-spec §2).
//
// Deliberately the same shape as CertificateExtension: a plain struct with no
// .cpp and no behaviour of its own. A certificate has at most one of these
// active at a time, so "active" and "not deleted" are the same fact; replacing
// one soft-deletes the old row and inserts a new one.
struct CertificateAttachment
{
    QString id;
    QString certificateId;

    // Relative to the managed data folder, never absolute (CLAUDE.md §6.9) —
    // that folder is copied between ship and office and between machines, so
    // an absolute path would be wrong the moment it travelled. Points at a
    // file or at a folder; nothing reading this needs to know which.
    QString relativePath;
};
