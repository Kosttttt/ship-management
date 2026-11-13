-- 014: the Confluence-style Document Management tables
-- (document-management-spec.md §4.2).
--
-- Five tables. Three of them are immutable — once a row is written it is
-- never changed — because they are the published record, and a published
-- revision that could be edited afterwards is not a record of anything.
--
-- Nothing derived is stored (CLAUDE.md §6.7): "has unpublished changes", a
-- page's current revision and a manual's current revision are all worked out
-- when they are needed. A stored "is current" flag is a second answer, free
-- to disagree with the first.

CREATE TABLE document_space (
    -- NOT NULL spelled out: SQLite only makes INTEGER PRIMARY KEY implicitly
    -- NOT NULL, never a TEXT one (CLAUDE.md §6.1).
    id          TEXT PRIMARY KEY NOT NULL,

    name        TEXT NOT NULL,

    -- MANUAL: the whole space gets Rev N, covering every page changed since
    -- Rev N-1 — ISM document control, where only a published revision counts.
    -- COLLECTION: each page is published on its own.
    kind        TEXT NOT NULL CHECK (kind IN ('MANUAL', 'COLLECTION')),

    -- Set only for a vessel's own automatic space; NULL for a company space
    -- (§4.2). No foreign key, for the same reason certificate.vessel_id has
    -- none: rows arrive from the other side of a future sync before their
    -- vessel does.
    --
    -- A vessel has at most one space, which is what makes "create them again
    -- and nothing is duplicated" a database guarantee rather than a habit.
    vessel_id   TEXT UNIQUE,

    description TEXT,

    -- Order in the spaces list, so it is the office's to arrange rather than
    -- alphabetical by accident.
    position    INTEGER NOT NULL DEFAULT 0,

    created_at  TEXT NOT NULL,
    created_by  TEXT NOT NULL,
    updated_at  TEXT NOT NULL,
    updated_by  TEXT NOT NULL,
    is_deleted  INTEGER NOT NULL DEFAULT 0,
    origin_node TEXT NOT NULL,
    revision    INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE document_page (
    id               TEXT PRIMARY KEY NOT NULL,
    space_id         TEXT NOT NULL,

    -- NULL means top of the tree. A page's parent is always in the same
    -- space; nothing here enforces that because nothing can write across
    -- spaces, and a CHECK cannot see another row.
    parent_id        TEXT,

    position         INTEGER NOT NULL DEFAULT 0,

    -- **The draft, and only the draft.** The published title and content are
    -- those of the page's latest revision and are deliberately not copied
    -- here: two copies of the same text are two things to keep in step.
    --
    -- Empty when there is no unpublished change. A page with no revision at
    -- all is a draft page, which is how a brand-new page starts.
    draft_title      TEXT NOT NULL DEFAULT '',
    draft_content    TEXT NOT NULL DEFAULT '',

    -- A published page in a Manual space marked for removal in the manual's
    -- next revision (§6.3). It stays visible, struck through, until that
    -- revision is published — because withdrawing a section is a change to
    -- the manual like any other and belongs in its revision note.
    withdraw_pending INTEGER NOT NULL DEFAULT 0,

    created_at       TEXT NOT NULL,
    created_by       TEXT NOT NULL,
    updated_at       TEXT NOT NULL,
    updated_by       TEXT NOT NULL,
    is_deleted       INTEGER NOT NULL DEFAULT 0,
    origin_node      TEXT NOT NULL,
    revision         INTEGER NOT NULL DEFAULT 1,

    FOREIGN KEY (space_id) REFERENCES document_space (id)
);

-- Every read of the tree is "this space's pages, by parent and position".
CREATE INDEX idx_document_page_space ON document_page (space_id, is_deleted, parent_id, position);

-- Manual spaces only: one row per published revision of the whole manual.
-- Immutable.
CREATE TABLE document_space_revision (
    id          TEXT PRIMARY KEY NOT NULL,
    space_id    TEXT NOT NULL,

    -- Rev N, counting from 1 within the space.
    number      INTEGER NOT NULL,

    -- Required for a manual revision (§6.2): a revision of a controlled
    -- manual with no statement of what changed is not much of a record. The
    -- UI insists; this column does not, because a package arriving from
    -- another installation is not this installation's to refuse.
    note        TEXT,

    created_at  TEXT NOT NULL,
    created_by  TEXT NOT NULL,
    updated_at  TEXT NOT NULL,
    updated_by  TEXT NOT NULL,
    is_deleted  INTEGER NOT NULL DEFAULT 0,
    origin_node TEXT NOT NULL,
    revision    INTEGER NOT NULL DEFAULT 1,

    FOREIGN KEY (space_id) REFERENCES document_space (id),
    UNIQUE (space_id, number)
);

-- One row per published revision of one page. Immutable: this is the text
-- that was in force, and the title it had at the time.
CREATE TABLE document_page_revision (
    id                TEXT PRIMARY KEY NOT NULL,
    page_id           TEXT NOT NULL,

    -- The page's own count, which is what a Collection page shows as "Rev 3".
    number            INTEGER NOT NULL,

    -- The manual revision this page revision belongs to, in a Manual space;
    -- NULL in a Collection space. This is what lets a page say "Rev 8" —
    -- the manual's number — rather than its own.
    space_revision_id TEXT,

    title             TEXT NOT NULL,
    content           TEXT NOT NULL,
    note              TEXT,

    created_at        TEXT NOT NULL,
    created_by        TEXT NOT NULL,
    updated_at        TEXT NOT NULL,
    updated_by        TEXT NOT NULL,
    is_deleted        INTEGER NOT NULL DEFAULT 0,
    origin_node       TEXT NOT NULL,
    revision          INTEGER NOT NULL DEFAULT 1,

    FOREIGN KEY (page_id) REFERENCES document_page (id),
    FOREIGN KEY (space_revision_id) REFERENCES document_space_revision (id),
    UNIQUE (page_id, number)
);

-- "The latest revision of this page" and "the revisions in this manual
-- revision" are the two reads that happen constantly.
CREATE INDEX idx_document_page_revision_page
    ON document_page_revision (page_id, is_deleted, number);
CREATE INDEX idx_document_page_revision_space_revision
    ON document_page_revision (space_revision_id);

-- Pictures, in the database rather than in a folder (§4.2), so a page and
-- its pictures are in every backup and travel together in a package later.
-- Immutable: editing a picture writes a new row, so an older revision still
-- shows the picture it was published with.
CREATE TABLE document_image (
    id          TEXT PRIMARY KEY NOT NULL,
    data        BLOB NOT NULL,
    mime_type   TEXT NOT NULL,

    created_at  TEXT NOT NULL,
    created_by  TEXT NOT NULL,
    updated_at  TEXT NOT NULL,
    updated_by  TEXT NOT NULL,
    is_deleted  INTEGER NOT NULL DEFAULT 0,
    origin_node TEXT NOT NULL,
    revision    INTEGER NOT NULL DEFAULT 1
);
