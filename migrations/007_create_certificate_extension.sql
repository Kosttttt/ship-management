-- 007: the certificate_extension table (certificate-extension-spec §4).
--
-- A short-term certificate issued by class or flag while a problem with the
-- real certificate's cycle is sorted out. It is a separate document, not a
-- change to the certificate: nothing in this table is ever copied onto
-- certificate.issue_date or certificate.expiry_date, and the certificate's own
-- row is never touched while one of these is active.
--
-- 005_create_endorsement.sql and 006_create_app_setting.sql are already
-- committed and stay untouched (CLAUDE.md §6.6).

CREATE TABLE certificate_extension (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id                 TEXT PRIMARY KEY NOT NULL,

    -- No REFERENCES clause, the same trade-off as endorsement.certificate_id
    -- and certificate.vessel_id, resolved the same way for consistency:
    -- CertificateExtensionRepository re-checks that the parent certificate
    -- exists and is in scope itself (certificate-extension-spec §4).
    certificate_id     TEXT NOT NULL,

    -- A calendar date, YYYY-MM-DD, timezone-free (CLAUDE.md §6.3).
    --
    -- NOT NULL, unlike certificate.expiry_date: a short-term certificate with
    -- no expiry date is not a meaningful concept, so there is no "does not
    -- expire" case to support here (certificate-extension-spec §4).
    expiry_date        TEXT NOT NULL,

    note               TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at         TEXT NOT NULL,
    created_by         TEXT NOT NULL,
    updated_at         TEXT NOT NULL,
    updated_by         TEXT NOT NULL,
    is_deleted         INTEGER NOT NULL DEFAULT 0,
    origin_node        TEXT NOT NULL,
    revision           INTEGER NOT NULL DEFAULT 1
);

-- Every read is "the active extension of one certificate": is_deleted = 0 is
-- what "active" means here, since resolving one soft-deletes it (§2).
CREATE INDEX ix_certificate_extension_certificate
    ON certificate_extension (certificate_id, is_deleted);
