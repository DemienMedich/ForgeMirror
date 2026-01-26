# ForgeMirror UI Tokens & Density Contract
(Agent Instruction – Mandatory)

This document defines exact UI density tokens and layout rules for ForgeMirror.
These values are not suggestions. They are constraints.

If visual preference conflicts with this document, THIS DOCUMENT WINS.

---

## 0) Purpose

- Enforce consistent UI density.
- Prevent UI “inflation” over time.
- Remove subjective decisions from layout and spacing.
- Keep ForgeMirror desktop-focused, compact, and professional.

---

## 1) Density Modes (Allowed Values)

ForgeMirror supports EXACTLY THREE density modes:

| Mode     | Use case |
|----------|----------|
| Compact  | Dashboards, Profile, Lists, Analytics |
| Normal   | Forms, Editors |
| Spacious | NOT ALLOWED unless explicitly requested |

Default mode for most screens: **Compact**

---

## 2) Global ImGui Tokens (Compact Mode)

### 2.1 Spacing

```cpp
ImGuiStyle& s = ImGui::GetStyle();

s.ItemSpacing      = ImVec2(8, 6);
s.FramePadding     = ImVec2(8, 4);
s.CellPadding      = ImVec2(6, 4);
s.WindowPadding    = ImVec2(10, 8);
s.IndentSpacing    = 16.0f;
Rules:

Vertical spacing is always tighter than horizontal.

Increasing vertical spacing requires justification.

2.2 Buttons
Type	Height rule
Primary CTA	default height
Secondary	-20% height
Icon-only	square, same height as secondary
Rules:

Exactly ONE Primary CTA per screen.

All others must be secondary or icon-only.

No two buttons may share equal visual weight unless explicitly required.

2.3 Headers & Text Hierarchy
Element	Rule
Screen title	1 line only
Section header	Small font, no heavy padding
Body text	Default font
Secondary text	Lower contrast OR smaller size (not both)
Forbidden:

Oversized headers

Multi-line titles

Headers with excessive top/bottom margin

3) Layout Tokens
3.1 Header Row (Mandatory Pattern)
Header MUST be exactly one row.

[ Context ▼ ]                               [ Status ] [ Icon ] [ ⋯ ]
Rules:

No multi-row headers.

No multi-column header panels.

No large buttons in header.

No duplicated action groups.

3.2 KPI Cards
Allowed only for summary metrics.

Token rules:

Fixed compact height.

Internal layout:

Small label

One value

No multi-line descriptions.

Minimal inner padding.

Forbidden:

Cards for simple lists

Cards for actions

Cards used to “fill space”

3.3 Lists (Recommendations, Top Items)
Property	Rule
Default items shown	max 3
Height	shrink to content OR fixed max
Container	NO heavy card
Header	thin text header
Rules:

Lists must never create empty vertical space.

If more items exist, use:

“Show more”

Scroll region with fixed height

3.4 Two-Column Main Area
Preferred structure:

| Left: Lists / Actions | Right: Donut / Highlights |
Rules:

Donut/charts must not float in empty space.

Right column content must be vertically compact.

Avoid single large centered widgets with unused margins.

4) Collapsible Sections
4.1 Details Panels
Must be collapsible.

Must be collapsed by default.

Collapsed state should show:

One-line summary if meaningful.

Forbidden:

Always-open detail sections

Detail sections above the fold

5) Navigation Discipline
Sidebar = pages.

Internal tabs only if page-specific.

NEVER duplicate sidebar pages inside a page.

Hard ban:

Profile page must NOT contain a “Tasks”, “Skills”, or similar page-level tab.

6) Action Redundancy Rules
If an action can be performed via:

dropdown

context menu

existing control

Then:

A dedicated button is forbidden.

Explicit ban:

The “Сменить” button MUST NOT exist anywhere.

Profile switching is ONLY via profile dropdown.

7) Status Indicators
Status UI (sync, timers, etc.) must be:

Informative

Compact

Secondary in visual hierarchy

Rules:

Text + small icon preferred.

No large buttons unless status is the primary screen purpose.

8) Tooltips (Mechanics Transparency)
Required tooltips for:

XP penalties

Focus bonuses

Recovery/warm-up logic

Degradation buffer

Tooltip limits:

Max 3 lines

Prefer showing current numeric values

No explanations longer than a short paragraph

9) Forbidden Patterns (Hard Stops)
The following are NOT allowed under any circumstances:

UI added to “fill space”

Increasing padding to “improve breathing”

Multiple primary CTAs

Multiple header rows

Decorative containers

Animations

New color palettes

Reintroducing removed controls (e.g. “Сменить”)

10) Agent Self-Check (Mandatory)
Before finishing any UI task, the agent must verify:

 Screen uses Compact density

 Header is single-row

 Exactly one primary CTA exists

 No duplicated navigation

 KPI cards are compact and summary-only

 Lists max 3 items visible

 Details collapsed by default

 No large empty gaps

 No banned controls reintroduced

If any check fails, the implementation is incorrect.

End of document.
