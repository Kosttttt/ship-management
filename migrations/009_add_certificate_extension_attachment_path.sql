-- 009: an extension's own document (certificate-attachment-spec §2).
--
-- Deliberately one nullable column on certificate_extension rather than a
-- second attachment table. An extension row is already a complete, immutable
-- historical record the moment it exists: nothing ever edits one in place, and
-- a new extension is always a brand-new row. So its document needs no archive
-- step and no "active" flag — it simply stays where it was put, tied to that
-- row, forever. Resolving the extension does nothing to it at all.
--
-- 007_create_certificate_extension.sql is already committed and is never
-- edited (CLAUDE.md §6.6), so the column arrives as its own migration — the
-- same shape 004 used for certificate.list_number.

ALTER TABLE certificate_extension ADD COLUMN attachment_path TEXT;
