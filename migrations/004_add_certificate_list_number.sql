-- 004: the company reference number on a certificate
-- (certificate-crud-spec §8.2, §8.4).
--
-- A short number the fleet uses in conversation — "Certificate 15D" — rather
-- than the full name. It is neither certificate_number (the official number
-- printed on the document, assigned by whoever issued it) nor the invisible
-- UUID in id.
--
-- 003_create_certificate.sql is already committed and is never edited
-- (CLAUDE.md §6.6), so the column arrives as its own migration.

ALTER TABLE certificate ADD COLUMN list_number TEXT
    -- Digits, then letters, and nothing else: "15", "3A", "15D". The format is
    -- not cosmetic — it is what lets the value be split into (number, letters)
    -- unambiguously and sorted correctly. As free text, "15D" would sort ahead
    -- of "3A", because '1' precedes '3' as a character.
    --
    -- Encoded with GLOB because SQLite has no regular expressions:
    --   * must begin with a digit
    --   * must contain nothing but digits and letters
    --   * must never have a digit after a letter
    -- The form's validator is the primary defence and the repository
    -- re-checks; this is the final backstop, the same three-layer shape the
    -- no-expiry rule already uses.
    CHECK (
        list_number IS NULL
        OR list_number = ''
        OR (
            list_number GLOB '[0-9]*'
            AND list_number NOT GLOB '*[^0-9A-Za-z]*'
            AND list_number NOT GLOB '*[A-Za-z][0-9]*'
        )
    );
