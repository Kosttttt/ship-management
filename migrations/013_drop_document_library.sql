-- 013: drop version 1.0 of the Document Management module
-- (document-management-spec.md §4.1).
--
-- Migration 012 created `document` and `document_version` for a library of
-- uploaded files with versions. The developer saw it working and decided the
-- module should be a simple Confluence instead — spaces, a page tree, pages
-- read and edited in place — so that design is removed rather than left
-- beside the new one.
--
-- 012 is **not edited**: it has already run on the developer's machine, and
-- CLAUDE.md §6.6 exists because an installed copy cannot be made to un-apply
-- a migration it has already recorded. The way to undo a table is a later
-- migration that drops it.
--
-- Any test documents entered in those tables are gone. The files themselves
-- stay in the old documents folder on disk, where they can be deleted by
-- hand — nothing here touches the filesystem.

-- The child first: document_version has a foreign key to document, and
-- dropping the parent while the child still references it would be refused
-- with foreign keys on (core/Database turns them on for every connection).
DROP TABLE IF EXISTS document_version;

DROP TABLE IF EXISTS document;
