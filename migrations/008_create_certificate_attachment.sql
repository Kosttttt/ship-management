-- 008: the certificate_attachment table (certificate-attachment-spec §2, §7).
--
-- The real document behind a certificate: a scan, a PDF, or a whole folder of
-- files. One row is active per certificate at a time, exactly the shape
-- certificate_extension already uses — attaching a replacement soft-deletes
-- the active row and inserts a new one, and the physical file or folder is
-- moved under archive/ rather than deleted (§2).
--
-- 007_create_certificate_extension.sql is already committed and stays
-- untouched (CLAUDE.md §6.6).

CREATE TABLE certificate_attachment (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id                 TEXT PRIMARY KEY NOT NULL,

    -- No REFERENCES clause, the same trade-off already resolved the same way
    -- for endorsement.certificate_id and certificate_extension.certificate_id:
    -- CertificateAttachmentRepository re-checks that the parent certificate
    -- exists and is in scope itself.
    certificate_id     TEXT NOT NULL,

    -- Relative to the managed data folder, never absolute (CLAUDE.md §6.9).
    -- That folder is copied between ship and office and between machines, so
    -- an absolute path recorded here would be wrong the moment it travelled.
    -- Points at a file or at a folder; nothing reading it needs to know which.
    relative_path      TEXT NOT NULL,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at         TEXT NOT NULL,
    created_by         TEXT NOT NULL,
    updated_at         TEXT NOT NULL,
    updated_by         TEXT NOT NULL,
    is_deleted         INTEGER NOT NULL DEFAULT 0,
    origin_node        TEXT NOT NULL,
    revision           INTEGER NOT NULL DEFAULT 1
);

-- Every read is "the active attachment of one certificate": is_deleted = 0 is
-- what "active" means here, since replacing one soft-deletes it (§2).
CREATE INDEX ix_certificate_attachment_certificate
    ON certificate_attachment (certificate_id, is_deleted);
