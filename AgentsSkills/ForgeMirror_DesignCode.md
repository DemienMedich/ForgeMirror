# ForgeMirror UI Design Code (Agent Instruction)

This document is the UI “design code” for ForgeMirror.  
It is a set of non-negotiable rules for layout density, hierarchy, and interaction patterns.  
When implementing or modifying UI, follow these rules strictly.

## 0) Goals

- Functional, desktop-first UI.
- Compact, tidy, not visually overloaded.
- One primary action per screen.
- Minimal “panel inflation”: avoid stacking multiple heavyweight containers.
- Changes must be incremental and consistent with existing style (dark theme + purple accent).

Non-goals:
- No full redesigns.
- No new visual themes.
- No decorative UI for its own sake.

---

## 1) Navigation Rules (No Duplication)

1. Left sidebar = primary navigation (pages).
2. Inside a page, sub-views are allowed ONLY if they are page-specific.
3. Never duplicate sidebar pages with internal tabs.
   - Example: if “Tasks” exists in sidebar, a “Tasks” tab inside “Profile” is forbidden.

Keyboard shortcuts (F1/F2/...) must continue to work.

---

## 2) Action Hierarchy Rules

### 2.1 One Primary CTA
Each screen must have exactly ONE visually primary CTA:
- Filled/accent button OR the strongest button style available.
- Placed above the fold.

All other actions must be secondary:
- Outline/ghost OR icon-only where appropriate.
- Smaller height and less visual weight.

### 2.2 Remove Redundancy
If an action is achievable via an existing control (dropdown/menu), do not add a dedicated button.
Example:
- Profile switching is done via profile dropdown.
- A separate “Сменить” button is redundant and must not exist.

---

## 3) Header Layout Standard

Header must be a SINGLE compact row.

**Left:**
- Current context selector (e.g. `Профиль: [Name ▼]`)

**Right:**
- Small status indicator (sync, etc.)
- 0–2 small icon buttons (e.g. Sync)
- Overflow menu `⋯` for secondary actions

Forbidden:
- Multi-row/multi-column headers with big sections like “Profile / Actions / Sync”.
- Large button groups in header.
- Multiple equal-weight header panels.

---

## 4) Density and Spacing Standards (ImGui)

This app is desktop-first. Use compact spacing.

### 4.1 Global Spacing Targets
- Reduce vertical spacing first, not font sizes.
- Prefer smaller `ItemSpacing.y` and `FramePadding.y` on dense screens.

Guideline (relative, not absolute numbers):
- Secondary buttons height < Primary button height
- KPI cards compact: minimal inner padding

### 4.2 Card/Panel Discipline
Use “cards” only for:
- KPI tiles (small summary metrics)
- Optional grouped details sections (usually collapsible)

Avoid cards for:
- Simple lists (“Next actions”, “Top focus”)  
  Use thin headers + list items instead.

---

## 5) Screen Composition Template (Default)

Most screens should follow this structure:

1) **Header row** (single row)
2) **KPI row** (optional)
3) **Main content** (two-column split preferred)
4) **Details** (collapsible, closed by default)

### 5.1 KPI Row Rules
- KPI tiles show:
  - small label
  - one strong value
- No multi-line explanations inside KPI tiles.

### 5.2 Lists (Recommendations, Top Items)
- Max 3 visible items by default.
- If more exists, add “Show more” or scroll region with fixed height.
- Avoid empty space: panels must shrink to content where possible.

### 5.3 Details Panel
- Always collapsible.
- Closed by default.
- Collapsed state should display a one-line summary if useful.

---

## 6) Status Indicators (Sync, etc.)

- Status must be informative but small.
- Show:
  - state (Synced / Syncing / Error)
  - last sync time when available
- A “Sync now” control should be icon-sized unless it is the only action on screen.

---

## 7) Tooltips for Complex Mechanics

Mechanics that look like “magic numbers” must have short tooltips:
- XP repetition penalty
- Focus bonus rules
- Recovery/warm-up after inactivity
- Degradation buffer

Tooltip rules:
- 1–3 lines
- Prefer showing current value examples
- No essays

---

## 8) What NOT to Do (Hard Bans)

- Do not add new panels to “fill space”.
- Do not add additional tabs that duplicate existing pages.
- Do not introduce new color palettes or major theme changes.
- Do not add animations.
- Do not increase padding to “make it breathe” if it reduces density.
- Do not add a “Сменить” button (profile switching must be via dropdown only).

---

## 9) Implementation Checklist (Agent Must Self-Verify)

Before finishing a UI change, verify:

- [ ] Header is single-row and compact
- [ ] Exactly one primary CTA exists
- [ ] No duplicated navigation (sidebar vs internal tabs)
- [ ] KPI row is compact and one-line per tile
- [ ] Lists show max 3 items by default (or fixed height)
- [ ] Details are collapsible and closed by default
- [ ] No large empty gaps around donut/charts/lists
- [ ] Tooltips exist for complex mechanics
- [ ] No new theme/colors/animations introduced

If any checkbox fails, adjust UI until it passes.

---

## 10) Preferred Refactor Pattern (Code Structure)

Keep drawing code readable and stable across frames:

- `DrawHeader()`
- `DrawKpiRow()`
- `DrawMainContent()`
- `DrawDetailsCollapsible()`

Avoid heavy allocations per frame and avoid layout flicker.

End of document.