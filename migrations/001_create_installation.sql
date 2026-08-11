-- 001: the installation table (CLAUDE.md §3).
--
-- Exactly one row, written once on first launch, describing whether this copy
-- of the application is the shore office or a vessel. Every repository consults
-- it to decide what the user is allowed to see.

CREATE TABLE installation (
    -- NOT NULL is spelled out on purpose. SQLite only makes a primary key
    -- implicitly NOT NULL when it is INTEGER PRIMARY KEY; a TEXT one accepts
    -- NULL. Every key in this project is a TEXT UUID (CLAUDE.md §6.1), so
    -- every table repeats this.
    id                 TEXT PRIMARY KEY NOT NULL,

    -- Guarantees a single row: the only value allowed is 1, and it is unique.
    -- A second INSERT fails at the database rather than silently giving the
    -- application two identities to choose between.
    singleton          INTEGER NOT NULL DEFAULT 1 UNIQUE CHECK (singleton = 1),

    installation_mode  TEXT NOT NULL CHECK (installation_mode IN ('OFFICE', 'VESSEL')),
    node_id            TEXT NOT NULL,
    vessel_id          TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at         TEXT NOT NULL,
    created_by         TEXT NOT NULL,
    updated_at         TEXT NOT NULL,
    updated_by         TEXT NOT NULL,
    is_deleted         INTEGER NOT NULL DEFAULT 0,
    origin_node        TEXT NOT NULL,
    revision           INTEGER NOT NULL DEFAULT 1,

    -- CLAUDE.md §3: vessel_id is a UUID on a vessel, and NULL at the office.
    CHECK (
        (installation_mode = 'OFFICE' AND vessel_id IS NULL) OR
        (installation_mode = 'VESSEL' AND vessel_id IS NOT NULL)
    )
);
