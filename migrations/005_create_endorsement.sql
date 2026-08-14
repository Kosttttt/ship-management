-- 005: the endorsement table (certificate-endorsement-spec §3).
--
-- The record that a survey actually happened. computeCertificateState()
-- matches these against the survey windows a certificate's dates imply, which
-- is what lets the software say whether a certificate is in good standing
-- rather than merely on file.
--
-- 004_add_certificate_list_number.sql is already committed and stays
-- untouched (CLAUDE.md §6.6).

CREATE TABLE endorsement (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id                 TEXT PRIMARY KEY NOT NULL,

    -- No REFERENCES clause, for the same reason certificate.vessel_id has
    -- none: nothing in this schema uses one yet, and EndorsementRepository
    -- re-checks that the parent certificate exists and is in scope itself.
    -- Revisit alongside that same decision (certificate-endorsement-spec §3).
    certificate_id     TEXT NOT NULL,

    -- Required, not optional: an annual endorsement never satisfies an
    -- intermediate requirement, so without knowing which kind a survey was,
    -- the calculation has nothing to match against
    -- (certificate-control-spec.md §3.4).
    --
    -- INITIAL and RENEWAL are valid stored values that a later step may
    -- write. The state calculation never reads them; they satisfy nothing.
    survey_type        TEXT NOT NULL
        CHECK (survey_type IN ('INITIAL', 'ANNUAL', 'INTERMEDIATE', 'RENEWAL')),

    -- A calendar date, YYYY-MM-DD, timezone-free (CLAUDE.md §6.3). A survey
    -- happened on a day, not at an instant.
    endorsement_date   TEXT NOT NULL,

    place              TEXT,
    surveyor           TEXT,
    result             TEXT,
    remarks            TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at         TEXT NOT NULL,
    created_by         TEXT NOT NULL,
    updated_at         TEXT NOT NULL,
    updated_by         TEXT NOT NULL,
    is_deleted         INTEGER NOT NULL DEFAULT 0,
    origin_node        TEXT NOT NULL,
    revision           INTEGER NOT NULL DEFAULT 1
);

-- Every read is "the endorsements of one certificate, not deleted".
CREATE INDEX ix_endorsement_certificate ON endorsement (certificate_id, is_deleted);
