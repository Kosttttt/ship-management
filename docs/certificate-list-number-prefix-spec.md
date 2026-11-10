# Certificate List Number — prefixed codes (`S1`, `P.6`, `EU.9`, …)

## 1. Why

`certificate-crud-spec.md` §8.2 restricted the "No." field to "digits,
optionally followed by letters" — `15`, `3A`, `15D` — specifically so the
value could always be split into (number, letters) unambiguously and sorted
correctly. That rule was written against the numbers already seen on V UNO's
plain list.

The developer's own company reference document — `S-14 Monthly
Certification Status Report`, used fleet-wide, not vessel-specific — shows
the real scheme is wider than that. Alongside the plain `1`–`169` (with
sub-letters like `9A`, `20F`, `146D`, already supported), it has eight
distinct prefixed annexes, each independently numbered from 1:

| Prefix  | Section (as the sheet names it)        | Range (today) |
|---------|------------------------------------------|----------------|
| *(none)*| Statutory / Class Surveys / Equipment     | `1`–`169`      |
| `S`     | Statements                                | `S1`–`S19`     |
| `P.`    | Plans and Documents                       | `P.1`–`P.48`   |
| `EU.`   | Compliance with EU Directives             | `EU.1`–`EU.9`  |
| `US.`   | USA — Federal                             | `US.1`–`US.12` |
| `USCA.` | USA — California                          | `USCA.1`–`USCA.4` |
| `USAL.` | USA — Alaska                              | `USAL.1`–`USAL.2` |
| `PC.`   | Panama Canal                              | `PC.1`–`PC.2`  |
| `IS.`   | Israel                                    | `IS.1`–`IS.2`  |

Not every vessel carries every annex — the developer was explicit that
different vessels legitimately have different certificates — but the
*numbering scheme itself* is the company's fixed standard, so the "No."
field needs to accept all of it, on any vessel, without knowing in advance
which annexes that vessel will ever use.

**Decided with the developer, not assumed:**
- The sort order groups by prefix and follows the S-14 sheet's own section
  order (below), not a generic alphabetical order. This is a deliberate
  trade-off: it needs a fixed priority list in code, so a brand-new prefix
  the sheet doesn't have yet won't have a defined position until that list
  is updated (see §3's fallback rule for what happens in the meantime).
- Scope is the format and sort fix only. No pick-list / catalogue of the
  ~200 S-14 entries is being built from this spec — every certificate is
  still typed in by hand, on whichever vessel actually has it. That is a
  separate, larger feature this spec deliberately does not include, and it
  would reopen the "no shared `certificate_type` table" decision from step 5
  if it's ever wanted later.

## 2. The widened format

`CertificateListNumber::pattern()` (`modules/certificates/domain/Certificate.h/.cpp`)
changes from:

```
(?:[0-9]+[A-Za-z]*)?
```

to:

```
(?:(?:[A-Za-z]{1,6}\.?)?[0-9]+[A-Za-z]*)?
```

In words: the whole field is still optional (blank stays valid, same as
today). When something is entered, it is an *optional* short letter prefix
(1–6 letters, with or without a trailing dot — `S` has none, `P.`/`EU.`/
`US.`/`USCA.`/`USAL.`/`PC.`/`IS.` all do, matched exactly as the sheet
writes them, not normalised), then one or more digits, then optional
trailing letters, exactly as before. Examples that must now validate:
`S1`, `S19`, `P.1`, `P.48`, `EU.9`, `US.12`, `USCA.4`, `USAL.2`, `PC.2`,
`IS.2` — alongside everything already valid (`15`, `3A`, `9A`, blank).

This is the one change needed everywhere the pattern is already shared from
(`CertificateListNumber::isValid()`, and the live `QRegularExpressionValidator`
on `m_listNumberEdit` in `CertificateEditForm.cpp`) — neither of those call
sites needs its own edit, by design; that sharing is the whole reason the
rule lives in the domain.

**Validation error message**, `CertificateValidation.cpp`, currently:

> "%1" is not a valid number.
>
> A number is digits, optionally followed by letters — 15, 3A, 15D.

Update the second line to:

> A number is an optional short prefix, then digits, then letters — 15, 3A,
> 15D, S1, P.6, EU.9.

## 3. Splitting and sorting

`splitListNumber()` (currently private to `Certificate.cpp`, number + letters
only) needs a third part, the prefix, normalised for comparison (uppercased,
trailing dot stripped — so `p.6` and `P.6` are the same certificate, the
same reasoning already applied to `3a`/`3A`). Suggested shape:

```cpp
void splitListNumber(const QString& value, QString* prefix, qlonglong* number, QString* letters)
```

parsed with one regex capturing three named groups — prefix (optional),
number, trailing letters — rather than the current hand-rolled digit-count
loop, since the loop can no longer assume position 0 is a digit.

`CertificateListNumber::lessThan()` compares, in order:

1. **Blank vs. set** — unchanged: blank sorts after everything set.
2. **Group priority** — the normalised prefix looked up in a fixed,
   ordered list matching the S-14 sheet's own section order:

   ```
   "", "S", "P", "EU", "US", "USCA", "USAL", "PC", "IS"
   ```

   A prefix *not* in this list (a future annex the sheet doesn't have yet)
   falls back to sorting **after every known group**, and among themselves,
   unknown prefixes sort alphabetically. This means a new prefix is never
   lost or rejected — it just sorts at the bottom until someone adds it to
   the list above, which is the one place this spec needs revisiting if the
   developer's fleet ever needs a ninth annex.
3. **Number** — unchanged, numeric comparison of the digit run.
4. **Trailing letters** — unchanged: case-insensitive first, case-sensitive
   as the final tiebreak for a strict ordering.

Worked example, in the order the list will actually show them:
`5` < `15D` < `169` < `S1` < `S19` < `P.1` < `P.48` < `EU.1` < `EU.9` <
`US.1` < `US.12` < `USCA.1` < `USCA.4` < `USAL.1` < `USAL.2` < `PC.1` <
`PC.2` < `IS.1` < `IS.2` < *(blank)*.

`CertificateListNumber::lessThan()`'s doc comment in `Certificate.h`, and
`pattern()`'s and `isValid()`'s, all currently say "digits, then letters" —
all three need updating to describe the prefix, pointing at this spec file
rather than re-explaining it inline.

## 4. The migration

A new migration, `011_widen_certificate_list_number_check.sql` — migration
004 is already applied and committed, and `CLAUDE.md` §6.6 says a migration
that has run is never edited; `MigrationRunner` enforces this itself by
checksum.

SQLite cannot `ALTER` a `CHECK` constraint in place. This needs the
standard SQLite rebuild: create a new `certificate` table with the wider
constraint, copy every row across unchanged, drop the old table, rename the
new one into place, and recreate `ix_certificate_vessel`. All of this runs
inside the one transaction `MigrationRunner::applyMigration()` already wraps
every migration file in — no new mechanism needed, just correct SQL — and
it will run automatically, on both the dev machine and the work laptop, the
next time each is opened after this ships, against whatever real
certificates are already stored there. **Test this migration against a real
copy of both machines' actual databases, not only an empty test database,
before it ships** — this is the first migration in the project that
rewrites an existing table rather than only creating one or adding a
column, and the dev machine's `ship-management.db` already has real,
re-imported V UNO data in it.

The new `CHECK` itself is **deliberately looser than the application-level
pattern above**, not an exact translation of it. SQLite's `GLOB` has no
repetition counts and no way to say "zero or more letters, then zero or one
dot, then one or more digits, then zero or more letters" as a single
pattern the way a regex can. Rather than fight `GLOB` into something
fragile, the backstop only checks the things a `GLOB` *can* express
reliably — exactly the same "defense in depth, inner layer is the one that
actually matters" reasoning already used elsewhere in this project (no
foreign key on `certificate.vessel_id`, for the same reason):

```sql
CHECK (
    list_number IS NULL
    OR list_number = ''
    OR (
        list_number NOT GLOB '*[^0-9A-Za-z.]*'   -- only letters, digits, dots
        AND list_number NOT GLOB '*.*.*'         -- at most one dot
        AND list_number GLOB '*[0-9]*'           -- at least one digit somewhere
    )
);
```

Say this plainly in the migration's own comment: this constraint is looser
than `CertificateListNumber::isValid()` on purpose, because the form's
validator and the repository's pre-write check are the real defence, the
same way the existing no-expiry `CHECK` already describes itself as "the
backstop," not the primary rule.

## 5. Tests

`tests/modules/certificates/tst_CertificateListNumber.cpp` already exists
and is the right place for all of this — it takes no database
(`ship_add_migrations()` is not called for it), so it is testing
`isValid()`/`lessThan()` as pure functions, same as today. Add:

- `isValid()` true for one example of each prefix in the table in §1, and
  for a lowercase variant of at least one (`p.6`).
- `isValid()` false for a prefix with no digits at all (`"P."`, `"S"` alone),
  a digit reappearing after letters (`"3A1"`, `"1A2B"` — the case that would
  make the split ambiguous, already refused today and must stay refused),
  and a prefix longer than 6 letters.

  **Correction, caught during implementation, not before:** this bullet
  originally named `"6P"` as the thing to keep refusing. That's wrong —
  `"6P"` is "digits, then letters," the plain `15D` shape, and has always
  been valid, before and after this change. The invariant that actually
  needs a test is digits-letters-digits (`3A1`, `1A2B`), corrected above.
- `lessThan()` proves the exact worked-example order from §3's end,
  including that an unknown prefix (anything not in the fixed list) sorts
  after every known one, and that two different unknown prefixes between
  themselves sort alphabetically.

A repository-level test (`tst_CertificateRepositoryValidation.cpp` or
wherever the existing no-expiry `CHECK` backstop is exercised) should prove
the new, wider `CHECK` actually accepts a prefixed value written directly
past the domain validator — the same role the existing no-expiry test plays
for that `CHECK`.

## 6. Everything this spec does *not* change

- No pick-list, catalogue, or import of the S-14 sheet's ~200 entries
  anywhere in the app — see §1's "decided with the developer" note.
- No change to how a certificate's survey rules, category, or anything else
  works — this is the "No." field's accepted text and sort order only.
- No change to CSV import/export beyond what naturally follows from the
  wider format being accepted (a prefixed value in the `No.` column parses
  and round-trips exactly like any other `list_number` already does — no
  code in `CertificateCsvFormat`/`CertificateCsvExporter` needs to change
  for this, since both already just carry `certificate.listNumber` through
  as plain text).
