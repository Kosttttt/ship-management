# InstallationContext storage: hold by value, not by reference

## 1. Purpose

A bug, not a new feature — and distinct from `certificate-survey-deadline-fix.md`,
which this is unrelated to.

Every repository class in this codebase takes an `InstallationContext` in its
constructor and currently stores it as a `const InstallationContext&` member.
A previous, untracked round of work found this causes a real dangling
reference, and partially fixed it: the shared `AttachmentFixture` in
`CertificateTestSupport.h` now holds its own `InstallationContext` by value,
which is correct and fixes every test suite built from that fixture. But the
same round left the underlying pattern — and most of the roughly 50 other
test call sites built directly from a repository constructor — untouched.
Independent verification since then found at least two more files where only
one call site out of several was actually fixed:
`tests/app/tst_AttachmentFolderSettings.cpp` (1 of 5 functions) and
`tests/modules/certificates/tst_EndorsementEditDelete.cpp` (1 of roughly 11
call sites, including its own `seedOne()` helper). The bug class is real and
still live almost everywhere it was never specifically reproduced.

Rather than continue hunting down and manually fixing call sites one file at
a time — which this round's own history shows doesn't reliably stay
complete — the fix goes in the repositories themselves.

## 2. The bug, precisely

Every affected repository constructor looks like:

```cpp
class SomeRepository {
public:
    SomeRepository(QSqlDatabase& db, const InstallationContext& installation)
        : m_db(db), m_context(installation) {}
    ...
private:
    QSqlDatabase&              m_db;
    const InstallationContext& m_context;   // <-- the problem
};
```

The natural way to write a test is:

```cpp
SomeRepository repo(db, officeContext());
```

`officeContext()` returns an `InstallationContext` **by value** — a
temporary. That temporary is destroyed at the end of this full statement
(the semicolon), the instant after `m_context` is bound to it. Every later
call on `repo` then reads freed memory through `m_context`. This survives an
unoptimised build, because the stack slot usually still holds the old bytes,
and breaks under `-O2`, where the slot is reused for something else — this
is exactly what turned into an access violation in
`tst_CertificateAttachmentNaming` and two silently-unfiltered VESSEL-mode
queries elsewhere in a Release build, the precise failure `CLAUDE.md` §3's
mandatory scope filtering exists to prevent.

## 3. The fix

For every class that currently stores its `InstallationContext` constructor
argument as `const InstallationContext&`, change the member to a plain
`InstallationContext`, held by value:

```cpp
class SomeRepository {
public:
    SomeRepository(QSqlDatabase& db, const InstallationContext& installation)
        : m_db(db), m_context(installation) {}   // now copies, doesn't bind
    ...
private:
    QSqlDatabase&       m_db;
    InstallationContext m_context;   // fixed
};
```

The constructor signature does **not** change — it still takes
`const InstallationContext&` as a parameter, which is the normal, cheap way
to pass one in. Only the member's own type changes, from a reference to a
value, so it now copies the argument once at construction instead of binding
to whatever the caller happened to pass.

Confirmed so far to need this: `VesselRepository`, `CertificateRepository`,
`EndorsementRepository`, `CertificateExtensionRepository`,
`CertificateAttachmentRepository`. The prior round's own report said "7
repositories" in total — **do not treat the five named above as the
complete list.** Grep the codebase for `const InstallationContext&` used as
a class member (not a constructor parameter — those are fine and unrelated)
and fix every one you find. `AppSettingRepository` is known **not** to take
an `InstallationContext` at all and is out of scope.

## 4. Why fix it here rather than at the ~50 test call sites

`InstallationContext` is a handful of immutable `QString`s with no setters
(`first-run-wizard-spec` §7) — copying it is free of consequence, and a copy
can never disagree with the original it was taken from. Fixing it at the
repository boundary closes the entire bug class in one small, reviewable
change. Naming a local at every call site instead would require touching
roughly 50 places by hand, relies on every future test (and future
developer) remembering the lifetime rule, and this round's own history is
direct evidence that reminder doesn't reliably hold — the prior fix round
covered perhaps 3 of the ~50 sites before moving on.

## 5. What does *not* change

- `InstallationContext` itself — still immutable, no setters, same fields.
- Every constructor's own signature — still `const InstallationContext&` as
  the parameter type. Only the member it's copied into changes.
- Any production *behaviour*. This is purely a storage/lifetime fix; nothing
  about what any repository returns or how it filters changes.
- `AppSettingRepository` — takes no `InstallationContext`, untouched.

## 6. Scope — files to touch

- The header (and `.cpp`, if the constructor is defined there) for every
  class the §3 grep turns up. Expect around 7; confirm the exact count and
  list rather than assuming the five named in §3 are all of them.
- `tests/modules/certificates/CertificateTestSupport.h` — the
  `AttachmentFixture::context` member and its comment (around lines
  236–253). The comment currently asserts "every repository stores the
  context as a `const InstallationContext&`" — once this fix lands, that
  claim is no longer true, and the comment needs to say so. Whether
  `AttachmentFixture` keeps holding its own `context` member is your call —
  it's harmless either way and arguably still clearer than four repeated
  `officeContext()`/`vesselContext()` calls, but it is no longer
  load-bearing for correctness the way the comment currently claims.
- `tests/app/tst_AttachmentFolderSettings.cpp` and
  `tests/modules/certificates/tst_EndorsementEditDelete.cpp` — each has
  exactly one call site already holding a named local (from the prior
  round) and several more that still pass a temporary straight into a
  constructor. Once repositories hold the context by value, the ones
  passing a temporary directly are no longer buggy — they become ordinary,
  correct code. You do **not** need to go name a local at each of them to
  close this out; that was specifically the approach being avoided (§4).
  Reviewing the existing single fixed call site in each file for
  consistency with the rest is optional cleanup, not a requirement.
- Any other file the §3 grep for the member pattern, or a search for a
  repository constructed directly from `officeContext()`/`vesselContext()`/
  similar, turns up with the same shape. Not a guess this time — the point
  of this fix is that it closes the remaining ~50 sites without having to
  individually locate and patch each one, so there's no need to enumerate
  them exhaustively; they're fixed by construction once §3 lands.

## 7. Verification

- Full clean rebuild from an actually-deleted `build` directory — confirm
  the path before deleting it, per `CLAUDE.md` §10.1 — in both Debug and
  Release configurations.
- `ctest`, the full suite, in both configurations.
- Specifically re-run `tst_CertificateAttachmentNaming`,
  `tst_AttachmentFolderSettings`, and `tst_EndorsementEditDelete` in the
  Release configuration, since Release is what originally surfaced the
  dangling-reference crash.
- Grep the codebase once more after the change to confirm no class still
  stores `InstallationContext` as a `const InstallationContext&` member —
  i.e., that the fix is actually complete rather than partial again.

## 8. Out of scope

- Anything from `certificate-survey-deadline-fix.md` — unrelated, already
  committed separately.
- `AppSettingRepository` — takes no `InstallationContext`.
- Any production behaviour change beyond the storage/lifetime fix itself.
- Renaming or restructuring `AttachmentFixture` beyond the comment
  correction in §6 — simplifying it further, if you want to, is a separate
  judgment call, not something this fix requires.
