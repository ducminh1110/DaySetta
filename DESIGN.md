# DESIGN — daysetta-web (as built)

Static site, no build step. Plain HTML + one CSS file + ~20 lines of vanilla JS (theme toggle only).
Works from file:// and from any static host. Two pages: `/` (long landing) and `/devlog/`.

## World

Warm paper editorial. The page is a lab notebook printed on cream paper: big serif headlines,
quiet sans body, mono for anything measured or logged. Illustrations are hand-cut paper scenes
(gouache/risograph grain) drawn as SVG in `art/`. One dark petrol band carries the honest status,
cut into the page with torn-paper edges. Papercut accents appear on exactly 3 spots: the hero
sticker ("no hypervisor. really."), the torn status-band dividers, and the taped "is this a VM?" note.

## Palette (own, not pangram's orange+green)

| token | light | dark | use |
|---|---|---|---|
| paper | #F6F1E6 | #17150F | page ground (+ css grain overlay) |
| paper-2 | #EFE7D4 | #1E1B13 | alternating sections, footer |
| card | #FDFAF2 | #211E16 | nav card, cards, stats |
| ink | #29241E | #EDE6D6 | text |
| ink-2 | #5C554A | #B3AA97 | secondary text (≥4.5:1 on all grounds) |
| verm | #BC3F26 | #E0684B | the one accent: links, primary CTA, "No VM." |
| verm-deep | #93301D | #C24A2E | hover, gpu tags |
| petrol | #12333B | #0E252C | status band, linux floor |
| ochre | #DFA83E | same | tape labels, sticker, highlights |
| sky | #8FB6CE | same | iokitd card/floor (text uses #3F6580 light) |
| moss | #47705B | #7BA98F | reims card (text uses #3E6350 light) |
| clay | #B8613A | #C97A50 | art + display tags (text uses #93492A light) |
| band-ink / band-dim | #F0EADB / #A9CEC3 | same | status band text (7.9:1 dim on petrol) |

## Type

- Display: **Gloock** (editorial serif, weight 400, tracking -.02em). h1 clamp(40→74px), h2 clamp(30→50px).
- Body: **Archivo** (variable 400–700), 17px/1.6.
- Mono: **Spline Sans Mono** (variable 400–600) for labels, tags, stats sublabels, diagram floors, log card.
- All self-hosted woff2 in `fonts/` (4 files, ~108KB total), font-display: swap.

## Layout rhythm

1240px max wrap, section padding clamp(64→120px). Sections alternate paper / paper-2 / petrol band.
Hero: copy left, art right (art has one big rounded corner, 26/90px), real-facts stat row below
(4 cells, 2 on phone). How-it-works: CSS floor diagram (5 floors + dashed wires) then 3 art cards.
Status: dark band, torn svg top+bottom, works/not-yet columns, mono log card. Roadmap: wide arch
illustration + 5 milestone cards (3/1 cols responsive). FAQ: taped note + native <details> accordion.
Devlog teaser: 3 post cards with mono tags. Footer: link row + Apple trademark line.

## Motion + a11y

Only micro-transitions (button/card hover lift, FAQ + rotate). `prefers-reduced-motion` kills all of
them and smooth scroll. Theme: pre-paint inline script reads localStorage → falls back to
prefers-color-scheme; toggle button with aria-pressed; no flash. Skip link, :focus-visible rings
(verm on paper, ochre on the band), all art has descriptive alt, diagram is role=img with full
aria-label, details/summary native semantics, headings hierarchical (sr-only h2 on devlog grid).
No horizontal overflow at 360px (measured 0px). All text pairs ≥4.5:1 (impeccable detect clean
except the deliberate cream ground, which the brief pins).

## Art

`art/*.svg` hand-authored, shared style line in ~/darling-recovery/wip/web/ILLUSTRATIONS.md.
feTurbulence+feDisplacementMap wobbly edges, fractalNoise grain overlay, misregistered window-pane
layer, blurred cast shadows. 5 pieces, 7–18KB each, ≥2 render-look-improve passes each, renders in
`shots/art/`. No text, no logos, no Apple UI in any piece.

## Known deliberate deviations

- Cream/beige ground flagged by impeccable's cream-palette rule: kept, the brief explicitly pins
  "warm off-white paper background" (LEAD STYLE NOTES).
- Em-dashes remain inside verbatim devlog quotes and the lw452 log line; trimmed everywhere else.