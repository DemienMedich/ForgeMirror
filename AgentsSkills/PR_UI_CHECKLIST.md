# PR UI Checklist (ForgeMirror)

This checklist is required for any PR that changes UI layout, spacing, or controls.

## A) Density / Layout
- [ ] Screen uses Compact density tokens where applicable
- [ ] Header is SINGLE ROW and compact
- [ ] No extra “action panels” in header
- [ ] Main area follows a clear structure (header → KPI → main → details)

## B) Actions / Hierarchy
- [ ] Exactly ONE primary CTA on the screen
- [ ] Secondary actions are smaller (secondary/icon-only)
- [ ] No redundant controls were added
- [ ] “Сменить” button does not exist anywhere (hard ban)

## C) Navigation
- [ ] No duplicated navigation (sidebar pages not repeated as internal tabs)
- [ ] Keyboard shortcuts still work

## D) Cards / Lists
- [ ] KPI cards are compact, summary-only (no multi-line descriptions)
- [ ] Lists show max 3 items by default
- [ ] No tall empty panels with 1–2 list items
- [ ] Donut/charts are not floating in large empty containers

## E) Details
- [ ] Details section is collapsible and collapsed by default
- [ ] Collapsed state shows a one-line summary (if relevant)

## F) Mechanics Transparency
- [ ] Tooltips exist for non-obvious mechanics (XP penalties, focus bonus, recovery, degradation)

## G) Visual Regression
- [ ] No new colors/theme changes
- [ ] No animations added
- [ ] No layout flicker across frames
- [ ] Tested at common window sizes (small/medium/large)

If any box is unchecked, the PR is not ready to merge.