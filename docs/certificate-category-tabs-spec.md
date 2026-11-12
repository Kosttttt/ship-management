# Certificate list — category tabs

Version 1.0. Requested by the developer during the Certificate Control
close-out; decided with him.

---

## 1. What

A row of tabs directly above the certificate table:

```
All (82) | Statutory (45) | Class (12) | Equipment (19) | Other (6)
```

Picking a tab shows only that category's certificates. **All** shows every
category, exactly as the list does today.

## 2. Decided with the developer

- **Label = name + count. A tab's text turns red when any certificate in
  it needs attention** — the same meaning the existing "Show only
  certificates needing attention" checkbox already uses: a row whose
  `CertificateState.display` is anything other than `DisplayStatus::Valid`.
  So a problem in another category is visible without clicking through.
- **Exports always cover the whole vessel**, whichever tab is open. Export
  CSV and Export PDF are unchanged. A report handed to a third party must
  never be missing certificates because of which tab happened to be open.

## 3. Behaviour

- **Order:** All, Statutory, Class, Equipment, Other — the category order
  the program already uses.
- **Always opens on All.** Not remembered between runs, on purpose: the
  first thing seen at start-up must never hide an expired or overdue
  certificate behind another tab. (Column widths are remembered; this is
  deliberately different.)
- **Works together with the attention checkbox.** Both filters apply. The
  counts follow what is visible: with the checkbox ticked, "Class (2)"
  means two class certificates needing attention.
- **Red follows the vessel, not the checkbox:** a tab is red when that
  category contains *any* certificate needing attention on this vessel,
  whether or not the checkbox is ticked.
- **Changing the vessel** keeps the current tab and recomputes counts and
  colours for the new vessel.
- **Changing tab clears the selection**, so Renew / Delete / Attachment
  can never act on a row that has just been hidden.
- **The alert banner's drill-down** ("show me what needs attention")
  switches to **All** as well as ticking the checkbox, so nothing it is
  pointing at is hidden in another tab.
- **Add Certificate** is unchanged — the dialog's category field decides
  the category, not the open tab. After saving, the list reloads; if the new
  certificate is in another category it simply appears under that tab's
  count.

## 4. Where it goes

- **Filtering and counting are decisions about what is shown**, so they go
  in the service layer, in `CertificateListRows` (where the attention filter
  already lives — `CLAUDE.md` §4 rule 3). `build()` gains the selected
  category (none = All) and also returns, per category, the visible count
  and whether anything in it needs attention — computed in the same pass so
  the numbers can never disagree with the rows.
- **The widget** only draws: a `QTabBar` above the table, labels and red
  text from what `build()` returned. If the tab bar's wiring pushes
  `CertificateListWidget.cpp` (379 lines) further over, move it into a small
  helper in `ui/` per `CLAUDE.md` §9.
- No schema change, no migration.

## 5. Tests

- `CertificateListRows` (no widgets): each category filter returns only
  that category; All returns everything; counts per category correct with
  the attention filter off and on; the "needs attention" flag set for a
  category with one expired certificate and clear for an all-valid one,
  and still set when the attention checkbox is off.
- Widget: five tabs in the order above; opens on All; picking a tab shows
  only that category's rows; changing tab clears the selection; the
  drill-down entry point selects All.
- Existing list tests (`tst_CertificateListWidget`,
  `tst_CertificateListStatus`, `tst_CertificateListColumns`) pass unchanged
  on the All tab — that is the proof the default view didn't change.
- Export tests unchanged and passing — the proof exports still cover the
  whole vessel.

## 6. Correction after the on-screen check — a red dot, not red text

**What happened.** The first build (`0fff375`) set each tab's text colour
with `QTabBar::setTabTextColor()`. The decision behind it was right — the
tooltip "Something in this category needs attention" appeared on the right
tabs — but on the developer's Windows 11 machine **no tab was drawn red**.
The program uses Qt's default style, which on Windows 11 paints tab labels
with its own colours and ignores a per-tab text colour. The tests only read
back the stored colour (`tabTextColor()`), never what was painted, so they
passed.

**Change.** Mark a tab that needs attention with a **small red dot icon**
(`QTabBar::setTabIcon()`), drawn in code with `QPainter` onto a small
`QPixmap` — no image file needed. Tab icons are drawn by every style. A
tab with nothing needing attention has no icon (`QIcon()`), so the label
doesn't shift between states beyond the dot appearing. Keep the tooltip.
Remove the `setTabTextColor()` call rather than leaving one that silently
does nothing on this machine.

A dot rather than text colour also doesn't rely on colour alone, which
helps anyone who doesn't distinguish red easily.

**Tests — check what is painted, not just what is stored.**
- Keep a property check (`tabIcon(index).isNull()` is false exactly for the
  categories needing attention, true for the others).
- Add a **render check**: `grab()` the tab bar with the program's default
  style (don't force a style in the test) and confirm red pixels appear
  inside the attention tabs' `tabRect()` and none inside the others. Run on
  Windows this uses the same Windows 11 style as the program — the test
  that would have caught the first build. Vacuity-check it against the old
  `setTabTextColor()` version: it must fail there.
