-- 012: the document and document_version tables
-- (document-management-spec.md §4.1) — the Document Management module's
-- library, step 1 of that module.
--
-- Two tables, not one: a document is the thing that exists and is referred to
-- ("SMS-03, Emergency Procedures"), and a version is one issue of it. Keeping
-- them apart is what makes a full history possible — the document's reference
-- and title stay put while version after version is added beneath it, and an
-- old version stays openable for ever.

CREATE TABLE document (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id            TEXT PRIMARY KEY NOT NULL,

    -- NULL means fleet-wide: a document that belongs to the company rather
    -- than to any one ship (§4.1). That is the whole reason this column is
    -- nullable, and the reason no query may be written as though it were not.
    --
    -- No foreign key, for the same reason certificate.vessel_id has none:
    -- rows arrive from the other side of a future ship-to-shore sync, and a
    -- constraint would refuse a document whose vessel has not been merged yet.
    vessel_id     TEXT,

    category      TEXT NOT NULL
        CHECK (category IN ('SMS', 'CIRCULAR', 'PLAN_MANUAL', 'REPORT')),

    -- Optional, and not unique: "SMS-03", "Circular 12/2026". Two vessels can
    -- legitimately hold the same reference, and so can the fleet and a vessel.
    reference     TEXT,

    title         TEXT NOT NULL,
    notes         TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at    TEXT NOT NULL,
    created_by    TEXT NOT NULL,
    updated_at    TEXT NOT NULL,
    updated_by    TEXT NOT NULL,
    is_deleted    INTEGER NOT NULL DEFAULT 0,
    origin_node   TEXT NOT NULL,
    revision      INTEGER NOT NULL DEFAULT 1
);

-- The list screen reads one vessel's documents plus the fleet-wide ones on
-- every reload, filtered to the rows that are not deleted.
CREATE INDEX idx_document_vessel ON document (vessel_id, is_deleted);

CREATE TABLE document_version (
    id                 TEXT PRIMARY KEY NOT NULL,

    document_id        TEXT NOT NULL,

    -- 1, 2, 3… within one document. The highest non-deleted sequence is the
    -- current version and every other is superseded — worked out when it is
    -- needed and never stored, because a stored "is current" flag goes wrong
    -- the moment two of them disagree (CLAUDE.md §6.7).
    sequence           INTEGER NOT NULL,

    -- Optional and purely informational: "Rev 5", "Issue 2". The program
    -- never parses it; sequence is what orders the history.
    revision_label     TEXT,

    -- A calendar date, YYYY-MM-DD, timezone-free (CLAUDE.md §6.3). Optional,
    -- and information only: nothing in this module is scheduled or alerted
    -- from a date (§1 of the spec).
    revision_date      TEXT,

    -- One line saying what changed, for the Versions list.
    change_note        TEXT,

    -- Relative to the documents folder, never absolute (CLAUDE.md §6.9): that
    -- folder is chosen per installation and re-chosen on another computer, so
    -- only the part below it may be stored.
    file_path          TEXT NOT NULL,

    -- What the file was called when it was chosen, kept so the Versions list
    -- can show something a person recognises rather than the stored name.
    original_file_name TEXT NOT NULL,

    created_at         TEXT NOT NULL,
    created_by         TEXT NOT NULL,
    updated_at         TEXT NOT NULL,
    updated_by         TEXT NOT NULL,
    is_deleted         INTEGER NOT NULL DEFAULT 0,
    origin_node        TEXT NOT NULL,
    revision           INTEGER NOT NULL DEFAULT 1,

    FOREIGN KEY (document_id) REFERENCES document (id)
);

-- Every read of a version is "this document's versions, newest first".
CREATE INDEX idx_document_version_document
    ON document_version (document_id, is_deleted, sequence);
