-- 006: the app_setting table (settings-app-setting-spec §3).
--
-- The alert thresholds that computeCertificateState() has been reading as
-- hardcoded 30/60/90 defaults since step 6, given a real home so they can be
-- edited. Core-owned (CLAUDE.md §5), and nothing about it is
-- module-specific.

CREATE TABLE app_setting (
    -- NOT NULL is spelled out on purpose. SQLite only makes a primary key
    -- implicitly NOT NULL when it is INTEGER PRIMARY KEY; a TEXT one accepts
    -- NULL. Every key in this project is a TEXT UUID (CLAUDE.md §6.1), so
    -- every table repeats this.
    id                    TEXT PRIMARY KEY NOT NULL,

    -- Guarantees a single row: the only value allowed is 1, and it is unique.
    -- A second INSERT fails at the database rather than silently giving the
    -- application two identities to choose between.
    --
    -- Identical to migration 001's mechanism on purpose: the two singleton
    -- tables in this schema behave the same way rather than each having its
    -- own idea of what "exactly one row" means.
    singleton             INTEGER NOT NULL DEFAULT 1 UNIQUE CHECK (singleton = 1),

    critical_days         INTEGER NOT NULL DEFAULT 30,
    expiring_soon_days    INTEGER NOT NULL DEFAULT 60,
    due_soon_days         INTEGER NOT NULL DEFAULT 90,

    -- Nullable ISO date; NULL means the daily toast has never been shown.
    -- Added now so step 9 has it; nothing reads or writes it yet.
    last_alert_toast_date TEXT,

    -- Standard audit and sync columns, CLAUDE.md §6.5.
    created_at            TEXT NOT NULL,
    created_by            TEXT NOT NULL,
    updated_at            TEXT NOT NULL,
    updated_by            TEXT NOT NULL,
    is_deleted            INTEGER NOT NULL DEFAULT 0,
    origin_node           TEXT NOT NULL,
    revision              INTEGER NOT NULL DEFAULT 1,

    -- The repository checks both of these first, with friendly messages.
    -- These are the final backstop for anything that reaches the database
    -- another way.
    CHECK (critical_days > 0 AND expiring_soon_days > 0 AND due_soon_days > 0),

    -- Strictly increasing, and this is a correctness requirement rather than
    -- tidiness: computeCertificateState() tests the thresholds in order, so
    -- an out-of-order set would make a whole severity tier unreachable.
    CHECK (critical_days < expiring_soon_days AND expiring_soon_days < due_soon_days)
);

-- Unlike installation, this row is seeded here rather than by a wizard: there
-- is no decision for a user to make, because the defaults are already the
-- right starting values. The table is never empty from this point on.
INSERT INTO app_setting (
    id, singleton, critical_days, expiring_soon_days, due_soon_days,
    last_alert_toast_date,
    created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision
) VALUES (
    -- A version-4 UUID built from SQLite's own randomness, since a migration
    -- cannot call QUuid::createUuid(). Each installation therefore gets its
    -- own id, which is the point of CLAUDE.md §6.1 — a fixed literal here
    -- would give every installation in the fleet the same one.
    lower(
        hex(randomblob(4)) || '-' ||
        hex(randomblob(2)) || '-4' ||
        substr(hex(randomblob(2)), 2) || '-' ||
        substr('89ab', abs(random()) % 4 + 1, 1) ||
        substr(hex(randomblob(2)), 2) || '-' ||
        hex(randomblob(6))
    ),
    1, 30, 60, 90,
    NULL,
    -- ISO-8601 UTC, the same shape every timestamp in this schema uses
    -- (CLAUDE.md §6.2).
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now'), 'SYSTEM',
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now'), 'SYSTEM',
    0,
    -- This migration runs before the first-run wizard, so there is no
    -- installation row to read a node id from yet. 'LOCAL' is what
    -- schema_version already uses for the same reason.
    'LOCAL',
    1
);
