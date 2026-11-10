-- 011: widen the "No." field to accept the company's prefixed reference
-- codes (certificate-list-number-prefix-spec.md §4).
--
-- Migration 004 added list_number with a CHECK encoding "digits, then
-- letters" — the rule as it was understood from V UNO's plain list. The
-- developer's own fleet-wide reference document (the S-14 Monthly
-- Certification Status Report) turns out to use eight prefixed annexes
-- alongside those plain numbers: S1-S19, P.1-P.48, EU.1-EU.9, US.1-US.12,
-- USCA.1-USCA.4, USAL.1-USAL.2, PC.1-PC.2, IS.1-IS.2. All sixty rows that
-- the first bulk import rejected were one of those.
--
-- 004 is already applied and committed, and a migration that has run is
-- never edited (CLAUDE.md §6.6) — MigrationRunner enforces that itself by
-- checksum. So the constraint is replaced here instead.
--
-- SQLite cannot ALTER a CHECK in place, so this is the standard table
-- rebuild: a new table with the wider constraint, every row copied across
-- unchanged, the old table dropped, the new one renamed into place, and the
-- index recreated. MigrationRunner::applyMigration() already wraps every
-- migration file in one transaction, so either all of this happens or none
-- of it does; no new mechanism is needed here, only correct SQL.
--
-- This is the first migration in the project that rewrites an existing table
-- rather than only creating one or adding a column. Nothing references
-- certificate with a FOREIGN KEY — endorsement, certificate_extension and
-- certificate_attachment all deliberately omit the REFERENCES clause — so
-- dropping and renaming cannot cascade into a child table or trip the
-- connection's PRAGMA foreign_keys.
--
-- The column list below is deliberately identical to 003 plus 004's
-- list_number, in the same order, including list_number last where ALTER
-- TABLE put it. The only intended difference between the old table and the
-- new one is the list_number CHECK.

CREATE TABLE certificate_new (
    id                           TEXT PRIMARY KEY NOT NULL,

    vessel_id                    TEXT NOT NULL,

    name                         TEXT NOT NULL,

    category                     TEXT NOT NULL
        CHECK (category IN ('STATUTORY', 'CLASS', 'EQUIPMENT', 'OTHER')),

    certificate_number           TEXT,
    applies_to                   TEXT,

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

    created_at                   TEXT NOT NULL,
    created_by                   TEXT NOT NULL,
    updated_at                   TEXT NOT NULL,
    updated_by                   TEXT NOT NULL,
    is_deleted                   INTEGER NOT NULL DEFAULT 0,
    origin_node                  TEXT NOT NULL,
    revision                     INTEGER NOT NULL DEFAULT 1,

    -- Declared here rather than arriving by ALTER TABLE as it did in 004, but
    -- still last, so the rebuilt table's column order is identical to the
    -- live one's.
    --
    -- The widened "No." backstop.
    --
    -- This constraint is deliberately LOOSER than
    -- CertificateListNumber::isValid(), and is not a translation of it.
    -- SQLite's GLOB has no repetition counts and no alternation, so there is
    -- no way to say "one to six letters, then an optional dot, then one or
    -- more digits, then zero or more letters" as a single reliable pattern
    -- the way a regular expression can. Rather than force GLOB into
    -- something fragile that would reject a legitimate code, this checks only
    -- what GLOB can express dependably: nothing but letters, digits and dots;
    -- at most one dot; at least one digit somewhere.
    --
    -- That is the same "defence in depth, and the inner layer is the one that
    -- actually matters" reasoning already used for certificate.vessel_id
    -- having no foreign key: the form's QRegularExpressionValidator refuses
    -- the keystroke, validateCertificate() re-checks before any write, and
    -- this is only the final net. Exactly the role 003's no-expiry CHECK
    -- describes itself as playing.
    list_number                  TEXT
        CHECK (
            list_number IS NULL
            OR list_number = ''
            OR (
                list_number NOT GLOB '*[^0-9A-Za-z.]*'
                AND list_number NOT GLOB '*.*.*'
                AND list_number GLOB '*[0-9]*'
            )
        ),

    -- Unchanged from 003: a certificate that never expires has no
    -- anniversary to schedule a survey against, so it cannot require one.
    CHECK (
        expiry_date IS NOT NULL
        OR (requires_annual_survey = 0 AND requires_intermediate_survey = 0)
    )
);

-- Every column named explicitly on both sides, so this cannot silently
-- depend on the two tables happening to declare them in the same order.
INSERT INTO certificate_new (
    id, vessel_id, name, category, certificate_number, applies_to,
    issue_date, expiry_date, issued_by, place_of_issue, is_interim,
    requires_annual_survey, requires_intermediate_survey, intermediate_mode,
    notes, created_at, created_by, updated_at, updated_by, is_deleted,
    origin_node, revision, list_number
)
SELECT
    id, vessel_id, name, category, certificate_number, applies_to,
    issue_date, expiry_date, issued_by, place_of_issue, is_interim,
    requires_annual_survey, requires_intermediate_survey, intermediate_mode,
    notes, created_at, created_by, updated_at, updated_by, is_deleted,
    origin_node, revision, list_number
FROM certificate;

DROP TABLE certificate;

ALTER TABLE certificate_new RENAME TO certificate;

-- Dropped with the old table, so it has to be recreated. Same name and same
-- columns as 003: every list query is still "the certificates of one vessel,
-- not deleted".
CREATE INDEX ix_certificate_vessel ON certificate (vessel_id, is_deleted);
