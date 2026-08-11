-- 002: the vessel table (first-run-wizard-spec §5).
--
-- Brought forward from step 4: the wizard cannot write a VESSEL installation
-- row without a vessel row for it to point at. Step 4 still owns editing it.
--
-- Column names are snake_case to match migration 001 and the audit columns.
-- The C++ side uses the IMO Compendium spelling required by CLAUDE.md §9 —
-- imoNumber, callSign, grossTonnage, portOfRegistry, flagState.

CREATE TABLE vessel (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id                 TEXT PRIMARY KEY NOT NULL,

    name               TEXT NOT NULL,

    -- UNIQUE does real work from step 4 onward: an OFFICE installation holds
    -- the whole fleet, entered directly or arriving by sync, and two vessels
    -- sharing an IMO number is a data error. Stored as TEXT so a leading zero
    -- can never be lost.
    imo_number         TEXT NOT NULL UNIQUE,

    -- Not collected by the wizard; filled in by the Vessel CRUD form in step 4.
    call_sign          TEXT,
    gross_tonnage      INTEGER,   -- a whole number, never a float (CLAUDE.md §6.8)
    port_of_registry   TEXT,
    flag_state         TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at         TEXT NOT NULL,
    created_by         TEXT NOT NULL,
    updated_at         TEXT NOT NULL,
    updated_by         TEXT NOT NULL,
    is_deleted         INTEGER NOT NULL DEFAULT 0,
    origin_node        TEXT NOT NULL,
    revision           INTEGER NOT NULL DEFAULT 1
);
