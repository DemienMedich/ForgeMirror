# ForgeMirror UI Grid & Sizing Spec (Agent Mandatory)

This spec defines the numeric grid, standard sizes, and allowed dimensions.
If a value is not listed, do not invent it. Pick the closest allowed token.

---

## 1) Grid System

### 1.1 Base grid
- Base unit: **8 px**
- Half-step allowed: **4 px**
- Anything else is forbidden (no 6px, 10px, 12px unless derived from 8/4).

### 1.2 Allowed spacing values
- **4, 8, 12, 16, 20, 24, 32**
(12 and 20 exist only as 8+4 and 16+4, do not invent new ones)

---

## 2) Standard Component Sizes

### 2.1 Buttons
Use these explicit heights (in pixels):
- **Primary:** 32
- **Secondary:** 26
- **Icon-only:** 26 (square 26x26)

Rules:
- Exactly one Primary per screen.
- Never mix Primary heights inside the same row.
- Header buttons must be Secondary or Icon-only only.

### 2.2 Input fields / Combo boxes
- Default height: **26**
- Header context selector height: **26**
- Inputs must match Secondary height unless screen is a form editor.

### 2.3 KPI Tiles (Compact)
- Height: **56**
- Inner padding: **12 (x), 8 (y)**
- Gap between tiles: **8**
- Content:
  - label (small)
  - value (strong)
  - optional tiny icon (small, aligned)

Forbidden:
- Multi-line descriptions inside KPI tiles
- Variable-height KPI tiles

### 2.4 Section Headers
- Height target (visual): **20–24**
- Top padding above header: **8**
- Bottom padding below header: **4**
Do not add extra empty “breathing bands”.

### 2.5 Lists (Recommendations, Top Focus)
- Default visible items: **3**
- Item height target: **18–22** (depends on font)
- List container max height:
  - 3 items + header + 8 padding = **~90–110**
- If more items exist:
  - add “Show more” OR
  - fixed scroll region (max height 140)

Forbidden:
- Tall list containers with 1–2 items leaving blank space.

### 2.6 Collapsibles (Details)
- Default state: **collapsed**
- Collapsed header row height: **26**
- Collapsed summary line: single line, clipped with ellipsis if needed.

---

## 3) Layout Templates

### 3.1 Header Row (One Row Only)
Height target: **34–40** (depending on font)
- Left: context dropdown
- Right: status + 0–2 icon buttons + overflow menu

Forbidden:
- second header row
- header split into 3 labeled panels
- big “action area” inside header

### 3.2 Main Area (Two Columns)
- Column gap: **16**
- Left: actionable list(s)
- Right: chart/donut + short highlights, compact

Donut rule:
- Donut must have adjacent content (list/highlights) so it doesn’t float in empty space.
- Avoid centering donut in a large empty panel.

### 3.3 Details Area
- Must be below main content
- Must be collapsible and collapsed by default

---

## 4) Hard Bans
- Any spacing not multiple of 8 or 4
- Buttons taller than 32 on Compact screens
- KPI tiles taller than 56 on Compact screens
- “Сменить” button anywhere (profile switching via dropdown only)
- Extra panels introduced to “fill space”

---

End of document.