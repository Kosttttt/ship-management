-- 003: the certificate table (certificate-crud-spec §4).
--
-- Every certificate carries its own survey rules. There is deliberately no
-- certificate_type catalogue to look them up from — see
-- certificate-control-spec.md §5 for why.
--
-- previous_certificate_id is deliberately absent: nothing can populate it
-- until the renewal workflow exists, so the migration that adds it belongs to
-- that step, not this one.

CREATE TABLE certificate (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id                           TEXT PRIMARY KEY NOT NULL,

    vessel_id                    TEXT NOT NULL,

    -- Free text, typed by whoever adds the certificate.
    name                         TEXT NOT NULL,

    -- A broad bucket for filtering and reporting, not authoritative data.
    category                     TEXT NOT NULL
        CHECK (category IN ('STATUTORY', 'CLASS', 'EQUIPMENT', 'OTHER')),

    certificate_number           TEXT,
    applies_to                   TEXT,

    -- Calendar dates, YYYY-MM-DD, timezone-free (CLAUDE.md §6.3).
    -- expiry_date is nullable: NULL means "does not expire".
    issue_date                   TEXT NOT NULL,
    expiry_date                  TEXT,

    issued_by                    TEXT,
    place_of_issue               TEXT,

    is_interim                   INTEGER NOT NULL DEFAULT 0,

    requires_annual_survey       INTEGER NOT NULL DEFAULT 0,
    requires_intermediate_survey INTEGER NOT NULL DEFAULT 0,
    intermediate_mode            TEXT NOT NULL DEFAULT 'ADDITIONAL'
        CHECK (intermediate_mode IN ('ADDITIONAL', 'REPLACES_ANNUAL')),

    notes                        TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at                   TEXT NOT NULL,
    created_by                   TEXT NOT NULL,
    updated_at                   TEXT NOT NULL,
    updated_by                   TEXT NOT NULL,
    is_deleted                   INTEGER NOT NULL DEFAULT 0,
    origin_node                  TEXT NOT NULL,
    revision                     INTEGER NOT NULL DEFAULT 1,

    -- The backstop for the no-expiry rule. A certificate that never expires
    -- has no anniversary to schedule a survey against, so it cannot require
    -- one. The form is the primary defence; this catches any other route in,
    -- such as a CSV import.
    CHECK (
        expiry_date IS NOT NULL
        OR (requires_annual_survey = 0 AND requires_intermediate_survey = 0)
    )
);

-- Every list query is "the certificates of one vessel, not deleted".
CREATE INDEX ix_certificate_vessel ON certificate (vessel_id, is_deleted);
