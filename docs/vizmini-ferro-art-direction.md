# vizMini ferro mode: art direction

Audience: Kevin, the motion designer, whoever builds the ferro renderer in `vizmini/`.
Scope: the look. Silhouette and pixel minimums, surface cues, the white field, the inside lights, the eyes, and how the enter/exit should look. Motion, timing, stances and patterns live in `docs/vizmini-ferro-motion-spec.md` (**MS**), written in parallel. This brief adopts MS's placement (he sits on a surface, r 15) and answers the **[look]** items in MS §6. Where the two disagree, §11 lists it.
Read from: `docs/party2-art-direction.md` (**AD2**), `docs/plan-party-mode.md`, `docs/party-mode-layout-spec.md`, `docs/party2-pattern-library.md` (**PL**), `vizbot/party_blob.h`, `vizmini/PLAN-v2.md`, `vizmini/strip_leds.h`, `vizmini/config.h`, MS.

Units: OLED pixels on the 128 x 64 SH1106 (one pixel is about 0.23 mm). `r` is the body radius (MS: S 12, **M 15**, L 18 px). Angles: clock positions on the body (12 = up). Sketches are true pixels at the right aspect ratio: each text cell is 1 px wide and 2 px tall, `█` is black (pixel off), a space is white (pixel lit). On a dark-mode viewer the glyphs show light; read them as black. Small sprite tables (drops, eyes, corner mask) use `#` for black and `.` for white, one character per pixel.

---

## 0. The pitch

**A drop of ink on a lightbox.** The screen is the magnet's light and he is the ink. The full-size vizBot is a black drop in the corner of a party. vizMini *is* the drop: one shape, the whole stage.

Three jobs, three carriers:

| Carrier | Says |
|---|---|
| The blob's shape and motion | body language: calm, curious, excited, the field |
| The inside LEDs | the feeling: his mood colour, the beat |
| Two tiny eyes, rarely | "it's still me" |

---

## 1. Silhouette at 128 x 64, 1 bit

### 1.1 Size and room

| Item | Value | Why |
|---|---|---|
| Body | MS sizes: S 12, **M 15**, L 18. Floor rest 34 x 26 px at M | medium beats small. M is the largest drop that leaves real air above him on the floor |
| Room for the field | on the floor: **37 px up**, the full floor sideways. Hanging: 37 px down | crowns go into that air; the long reaches go along the floor or up to the glass |
| Envelope | every part stays **2 px** in from every screen edge, including the flat floor contact (MS §1.2) | a part touching the edge reads as cropped. Exceptions: Peekaboo (sliding off on purpose) and the enter/exit |
| Total black | rest ≈ 9 %; events ≤ 22 %; "wild" stacks ≤ 30 % for ≤ 2 s | past a third black it stops reading as a drop on light and starts reading as a dark screen |
| Look features by size | glint and eyes need r ≥ 14. **At S both are off** | each needs ≥ 4 px of black around it |

### 1.2 The +1 px rule: black on a lit field reads thin

Light from lit pixels scatters in the panel glass and whatever is in front of it, and the eye overrates bright areas next to dark ones (irradiation). So a black shape on a white OLED looks thinner than the same shape in white on black. Today's face gets the flattering side of that; ferro gets the other side.

**Every black minimum below is 1 px fatter than the same part would need in white on black.** If the OLED sits behind the translucent plastic rather than an open cutout or clear window, add 1 more px (open question 2).

### 1.3 Minimums

| Part | Minimum | Typical | Notes |
|---|---|---|---|
| Tentacle core, anywhere | **4 px** | root 9 to 12, mid 6, neck 4 to 5 | retract by length, never by width (AD2) |
| Tentacle tip bulb | **6 px** across and ≥ 1.5 x the neck | 7 to 8 (MS: bulb radius ≥ 3) | the bulb is the cute; it must be visibly wider than the neck |
| Root fillet | concave, radius ≥ 3 px | 3 to 4 | no hard corner anywhere a part meets the body |
| Spike base | 6 px | 7 to 8 | |
| Spike tip | **2 px**, flat or round | 2 | never a 1 px point (§1.4) |
| Droplet | **5 px** across | 6 to 8 | under 5 it reads as dust or a dead pixel |
| Bridge (a drop flowing home) | **3 px** waist | 3 to 5 | under 3 it pinches and breaks; it never thins further |
| White gap between black parts | **2 px** | 3+ | under 2 they merge (liquid union), never a 1 px slit |
| White inside black | glint (§2.1) and eyes (§5) only | | any other hole in the union is a bug |
| Edge wave / dent | 1.5 px, or off (MS agrees) | | a 1 px wave just makes the rim crawl |

### 1.4 How spikes read at 1 bit

- **Profile: a bullet, not a needle.** Plump, narrowing late: `w(t) = 2 + (base − 2)·(1 − t/h)^0.7`, with t from the rim (0) to the tip (h). Concave flanks only at the root.
- **2 px tip.** A 1 px point toggles on and off as the spike moves by a fraction of a pixel, so tips sparkle. A 2 px tip always covers at least one pixel per row.
- **Height to base:** h ≤ 1.7 x base. A 12 px spike needs a 7 px base; 15 px needs 9.
- **Heights:** nub 3 to 5 px (round top), spike 6 to 12, tall 13 to 16 (Grand Crown centre only).
- **Spacing is the big one at this size.** Spikes 7 px or taller need their bases ≥ 7 px apart along the rim, which at M is **≥ 30° (every other MS slot, 34°)**. So a tall crown holds **5** at most. MS's 3-vertex (17°) slot spacing works only for spikes ≤ 6 px. At half height there must be ≥ 2 px of white between neighbours.

A 7-spike crown at 17° (left: it fuses into a stepped lump) vs a 5-spike crown at 30° (right: it reads as a crown). M floor body, 1:1:

```
┌────────────────────────────────────────────────────┐   ┌────────────────────────────────────────────────────┐
│                                                    │   │                         ▄▄                         │
│                                                    │   │                        ████                        │
│                        ▄██▄                        │   │                        ████                        │
│                   ██▄  ████  ▄██                   │   │             ▄██▄      ▄████▄      ▄██▄             │
│               ▄  ▄██████████████▄  ▄               │   │              ████▄    ██████    ▄████              │
│              ████████████████████████              │   │              ██████▄  ██████  ▄██████              │
│           ▄  ████████████████████████  ▄           │   │               ██████████████████████               │
│          ████████████████████████████████          │   │        ▄▄     ██████████████████████     ▄▄        │
│          ▀██████████████████████████████▀          │   │       ▄████████████████████████████████████▄       │
│           ██████████████████████████████           │   │        ▀██████████████████████████████████▀        │
│           ██████████████████████████████           │   │          ████████████████████████████████          │
│          ████████████████████████████████          │   │          ████████████████████████████████          │
│         ██████████████████████████████████         │   │         ██████████████████████████████████         │
│         ██████████████████████████████████         │   │         ██████████████████████████████████         │
│         ▀████████████████████████████████▀         │   │         ▀████████████████████████████████▀         │
│          ████████████████████████████████          │   │          ████████████████████████████████          │
│           ▀████████████████████████████▀           │   │           ▀████████████████████████████▀           │
│            ▀██████████████████████████▀            │   │            ▀██████████████████████████▀            │
│               ▀████████████████████▀               │   │               ▀████████████████████▀               │
│                                                    │   │                                                    │
└────────────────────────────────────────────────────┘   └────────────────────────────────────────────────────┘
```

Bad tip (left, 1 px point) vs good tip (right, 2 px). Both 7 px base, 11 px tall:

```
┌────────────────────────────────┐
│                                │
│       ▄█▄            ▄██▄      │
│       ███            ████      │
│      █████          ▄████▄     │
│      █████          ██████     │
│    ▄███████▄       ▄██████▄    │
│████████████████████████████████│
└────────────────────────────────┘
```

### 1.5 Tentacles

- **Plump, round-tipped**, profile as AD2: `w(u) = w_neck + (w_root − w_neck)·(1 − u)^1.6`. Sharp tips never point at what he wants.
- **Length is MS's call** (Bloom 24 px, Wall Reach up to 60 px). The look sets the widths: the core never drops below 4 px at any length, and anything over 40 px long keeps ≥ 5 px.
- **One gentle C**, ≤ 25° of bend, curving like a field line. Mirror pairs are exact.
- **Glass contact keeps the gap.** When a Wall Reach bulb "touches the glass" and flattens (MS), it stops 2 px short of the edge, the same as his floor contact. The flattening says "touching"; the gap stops it reading as cropped.
- **The long lower flank is the meniscus.** A tentacle leaving the body near the waist blends into it over 15 to 20 px (see the dipole sketch). That's what makes it liquid being pulled, not an arm. Keep it.

Left tentacle tip, 1:1 (8 px bulb on a 4.5 px neck):

```
┌──────────────────────────────┐
│                              │
│                              │
│     ▄████▄                   │
│    ████████▄▄                │
│    ███████████████▄▄▄▄▄▄▄▄▄▄▄│
│     ▀████▀███████████████████│
│              ▀▀▀█████████████│
│                    ▀▀▀▀▀█████│
└──────────────────────────────┘
```

### 1.6 Droplets

- **≤ 3 free droplets at once**, 5 to 8 px. Patterns that are made of drops (MS Rain 4 to 6, Pearl Chain 3) may go over, because they're evenly spaced and read as one pattern.
- **Use hand-drawn sprites, not computed circles.** At these sizes a computed circle comes out squarish or lopsided depending on the sub-pixel centre. Snap the centre to whole pixels and stamp one of these:

```
d5        d6         d7          d8
.###.     .####.     ..###..     ..####..
#####     ######     .#####.     .######.
#####     ######     #######     ########
#####     ######     #######     ########
.###.     ######     #######     ########
          .####.     .#####.     ########
                     ..###..     .######.
                                 ..####..
```

- **Stretch while flying** by stamping a sprite 1 px longer along the travel axis (d6 becomes 7 x 6), never by thinning it.
- **A drop never shrinks below 5 px on screen.** It goes home through a bridge (≥ 3 px waist) and is absorbed at the rim, or it stays out.

Drop flowing home with its bridge (left), free drop (right), 1:1:

```
┌────────────────────────────────────────────────────────────────────┐
│                                                                    │
│                                                          ▄▄▄▄      │
│      ▄▄██▄▄                                             ██████     │
│     ▄██████▄                                            ██████     │
│     ▀███████                                             ▀▀▀▀      │
│      ▀▀██████▄                                                     │
│           ▀█████▄                                                  │
│             ██████▄          ▄▄▄▄▄▄▄▄                              │
│              ▀██████▄    ▄██████████████▄                          │
└────────────────────────────────────────────────────────────────────┘
```

### 1.7 Keeping a 34 px drop from turning into pixel mess

1. **Few, medium parts.** Body, ≤ 2 tentacles (3 in a wild stack, the third short), ≤ 5 tall spikes, ≤ 3 free drops. Kevin's "medium over small" counts double here: 3 px is under a millimetre.
2. **One silhouette.** One union, filled as spans. No outlines, no layers, no holes except the glint and eyes.
3. **Waves are ≥ 1.5 px or off** (MS dropped n = 5 for this reason; agreed).
4. **Calm edges.** A span end that pops out one pixel for one frame is noise. Hold span ends with a small hysteresis while the shape is calm. Test: at rest, no rim pixel toggles more than twice a second (AD2 bench criterion 2).
5. **Symmetry is the order.** Mirror about his own vertical axis on the floor (MS §2.4). A busy symmetric frame reads as a field; the same pixels at random angles read as dirt.

### 1.8 Key poses (full screen, 1:1)

The corner marks are the rounded-corner mask (§3.1).

**A. Rest on the floor.** M, flat 14 px contact patch 2 px above the bezel, glint at 10:30.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│█▀▀                                                                                                                          ▀▀█│
│▀                                                                                                                              ▀│
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                       ▄▄▄████████████▄▄▄                                                       │
│                                                    ▄██████▀███████████████▄                                                    │
│                                                  ▄█████▀  ▄█████████████████▄                                                  │
│                                                 ██████  ▄█████████████████████                                                 │
│                                                ███████▄████████████████████████                                                │
│                                               ██████████████████████████████████                                               │
│                                               ██████████████████████████████████                                               │
│                                               ▀████████████████████████████████▀                                               │
│                                                ████████████████████████████████                                                │
│                                                 ▀████████████████████████████▀                                                 │
│                                                  ▀██████████████████████████▀                                                  │
│▄                                                    ▀████████████████████▀                                                    ▄│
│█▄▄                                                                                                                          ▄▄█│
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

**B. Rosensweig crown (M).** 5 spikes, 13 / 10 / 6 px, 30° apart. The crown sketch in §1.4 is this pose.

**C. Dipole (Float stance, inside Lava Lamp or Twins).** Two mirrored reachers along the wide axis with 8 px bulbs, a 3-spike crown above and a nub below. A bar magnet's field: the hero image of ferro mode.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│█▀▀                                                                                                                          ▀▀█│
│▀                                                                                                                              ▀│
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                              ▄▄▄▄                                                              │
│                                                         ▄    ████    ▄                                                         │
│                                                        ███▄▄██████▄▄███                                                        │
│                                                        ████████████████                                                        │
│                                                       ▄████████████████▄                                                       │
│                                                     ▄▄██████████████████▄▄                                                     │
│             ▄████▄                                ▄██████▀▀████████████████▄                                ▄████▄             │
│            ████████▄▄                           ▄██████▀  ███████████████████▄                           ▄▄████████            │
│            ███████████████▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄█████████ ▄▄████████████████████████▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄███████████████            │
│             ▀████▀██████████████████████████████████████████████████████████████████████████████████████████▀████▀             │
│                      ▀▀▀██████████████████████████████████████████████████████████████████████████████▀▀▀                      │
│                            ▀▀▀▀▀██████████████████████████████████████████████████████████████▀▀▀▀▀                            │
│                                      ▀▀▀▀▀▀████████████████████████████████████████▀▀▀▀▀▀                                      │
│                                                ▀██████████████████████████████▀                                                │
│                                                  ████████████████████████████                                                  │
│                                                   ▀████████████████████████▀                                                   │
│                                                     ▀▀██████████████████▀▀                                                     │
│                                                         ▀▀██████████▀▀                                                         │
│                                                             ██████                                                             │
│                                                              ████                                                              │
│                                                              ▀▀▀▀                                                              │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│▄                                                                                                                              ▄│
│█▄▄                                                                                                                          ▄▄█│
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

**D. Wild stack (floor).** Grand Crown (15 / 11 / 7 px at 30°) + two low feelers along the floor + a mirrored pair of flung drops. About the ceiling for "stacked craziness": still one readable drop.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│█▀▀                                                                                                                          ▀▀█│
│▀                                                                                                                              ▀│
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                    ▄▄▄▄                                                ▄▄▄▄                                    │
│                                   ██████                                              ██████                                   │
│                                   ██████                                              ██████                                   │
│                                    ▀▀▀▀                                                ▀▀▀▀                                    │
│                                                                                                                                │
│                                                                                                                                │
│                                                              ▄██▄                                                              │
│                                                              ████                                                              │
│                                                              ████                                                              │
│                                                   ▄██▄      ██████      ▄██▄                                                   │
│                                                   ▀████▄    ██████    ▄████▀                                                   │
│                                                    ██████▄▄▄██████▄▄▄██████                                                    │
│                                                    ▀██████████████████████▀                                                    │
│                                             ▄▄▄▄    ██████████████████████    ▄▄▄▄                                             │
│                                            ▀██████████████████████████████████████▀                                            │
│                                              ▀██████████████████████████████████▀                                              │
│                                               ██████████████████████████████████                                               │
│                                              ▄██████████████████████████████████▄                                              │
│                  ▄████▄                      ████████████████████████████████████                      ▄████▄                  │
│                  ████████▄▄                 ▄████████████████████████████████████▄                 ▄▄████████                  │
│                  ▀██████████████▄▄▄▄▄▄▄▄▄▄▄████████████████████████████████████████▄▄▄▄▄▄▄▄▄▄▄██████████████▀                  │
│                       ▀▀██████████████████████████████████████████████████████████████████████████████▀▀                       │
│                           ▀▀▀████████████████████████████████████████████████████████████████████▀▀▀                           │
│                                  ▀▀▀▀████████████████████████████████████████████████████▀▀▀▀                                  │
│▄                                                ▀▀▀▀▀▀██████████████████▀▀▀▀▀▀                                                ▄│
│█▄▄                                                                                                                          ▄▄█│
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

**E. Hang + drip (ceiling).** A pendant drop, a drip on a neck about to pinch, and the last drop on its way down.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│█▀▀                                                                                                                          ▀▀█│
│▀                                                   ████████████████████████                                                   ▀│
│                                                  ▄██████████████████████████▄                                                  │
│                                                 ▄████████████████████████████▄                                                 │
│                                                 ██████████████████████████████                                                 │
│                                                ▄██████████████████████████████▄                                                │
│                                                ████████████████████████████████                                                │
│                                                 ██████████████████████████████                                                 │
│                                                 ██████████████████████████████                                                 │
│                                                  ▀██████████████████████████▀                                                  │
│                                                   ▀████████████████████████▀                                                   │
│                                                     ▀████████████████████▀                                                     │
│                                                        ▀▀████████████▀▀                                                        │
│                                                            ▀██████▀                                                            │
│                                                             ██████                                                             │
│                                                            ▄██████▄                                                            │
│                                                            ████████                                                            │
│                                                            ▀██████▀                                                            │
│                                                              ▀▀▀▀                                                              │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                                                                                                │
│                                                              ▄▄▄▄                                                              │
│                                                             ██████                                                             │
│                                                             ██████                                                             │
│                                                              ▀▀▀▀                                                              │
│                                                                                                                                │
│▄                                                                                                                              ▄│
│█▄▄                                                                                                                          ▄▄█│
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Liquid cues without grey

### 2.1 The glint: yes, as an arc, never a dot

Without a highlight he's a flat ink stain. One small white crescent says "glossy and round".

| Item | Value |
|---|---|
| Shape | a 1 px arc, **2 px thick in its middle third**, about 35° of arc (6 px long at M, 8 at L) |
| Where | at 10:30 on the body, at radius **r − 4.5 px**, so ≥ 4 px of black stays between it and the white field. Light from the upper left, as on vizBot |
| How it moves | **locked to the light, not to the body.** It stays at 10:30 in every stance (hanging from the ceiling too: the light is in the room), riding the local radius there. When he travels it slides up to 15° against the motion, then eases back: liquid inertia |
| Hides when | the eyes are up (§5); \|stretch\| > 0.10 or E > 0.6 (MS §6); squash past ±0.2; the radius at 10:30 is more than 3 px off rest (a spike or tentacle root there); at size S; during enter and exit |
| Never | on a tentacle, spike, bulb or drop. A white dot on a bulb is a third eye (AD2 §2.4) |

- **Why an arc:** a 2 x 2 white dot inside a black blob is an eye, and one dot is a cyclops. The arc is the one white-in-black shape that can't be read as a face.
- **Why r − 4.5 and not MS's r − 3:** at r − 3 only 2 px of black separates the glint from the white field, and the lit field blooms into that, so the glint reads as a crack or a notch in the rim.
- About 12 pixels cleared after the body fill: free. Ship it behind a toggle for one bench A/B.

The M rest drop with its glint, 1:1:

```
┌────────────────────────────────────────┐
│                                        │
│                                        │
│           ▄▄▄████████████▄▄▄           │
│        ▄██████▀███████████████▄        │
│      ▄█████▀  ▄█████████████████▄      │
│     ██████  ▄█████████████████████     │
│    ███████▄████████████████████████    │
│   ██████████████████████████████████   │
│   ██████████████████████████████████   │
│   ▀████████████████████████████████▀   │
│    ████████████████████████████████    │
│     ▀████████████████████████████▀     │
│      ▀██████████████████████████▀      │
│         ▀████████████████████▀         │
│                                        │
└────────────────────────────────────────┘
```

### 2.2 Dither: none

No dither anywhere in ferro mode. On a moving 1-bit shape, dither swims and reads as a mesh, not as grey.

### 2.3 Floor line or contact shadow: none

- **Contact shadow:** in 1 bit it's either black (it merges into him and reads as a fatter, flatter drop) or dithered (ruled out). No.
- **Floor line:** a fixed 1 px line makes him a thing on a shelf instead of a drop in a field, and as a static unlit line it wears unevenly (§3.5). No.
- **The contact patch does the job.** MS's flat, wetted 14 px patch, 2 px above the bezel, reads as "sitting on the bottom of the screen" without drawing anything. Agreed as specced.

### 2.4 What carries "liquid" instead

All black and free: concave root fillets (no corners except spike tips), bulbs on necks, necks that pinch before a drop leaves, bridges that thin and snap, drops that merge on contact, and the flat contact patch. Plus the jiggle (MS). On 1 bit the shape language does what the rim, sheen and shadow do on the colour LCD.

---

## 3. The white field

### 3.1 Pure white, with rounded corners

**Recommendation: pure white, no pattern, plus a 5-pixel rounded-corner mask in each corner.**

```
top-left corner (mirror for the others)
###
#..
#..
```

- It turns the hard rectangle into a lit lens or a light table, which suits a round drop and the round bot. Twenty pixels, always black, so no wear.
- **Rejected:** a sparse dot pattern (dust or dead pixels, and static, so uneven wear); a dithered vignette (grey mesh that swims where he crosses it); a 1 px black border (a UI window, fighting the shell's own window edge).
- **One event layer, off by default: "Field Lines".** For a hero beat (the Dipole, Magnet Approach), up to 60 black 2 px dashes laid along precomputed, mirror-symmetric field lines looping from one pole of him to the other, like iron filings around a bar magnet. They appear over 300 ms, hold ≤ 2.5 s and drop out in random order. Dashes point along the line (horizontal, vertical or diagonal pairs). About 120 pixel writes. This is the "stacked craziness" layer only ferro mode can do.

### 3.2 Brightness: lower the panel, keep the contrast

On an OLED the "contrast" register (SH1106 0x81) is segment current, i.e. brightness. Black is a pixel that's off and stays true black at any setting. **Lowering it in ferro mode costs no contrast at all**; it only lowers the glare.

| | Face (today) | Ferro |
|---|---|---|
| Lit pixels | ~30 % (two R 20 eyes) | ~80 to 90 % |
| Register | `DEFAULT_CONTRAST` 180, or the user's value | **0.55 x the user's value, clamped 40 to 140** (180 gives 99) |
| Idle dim (open question 4) | n/a | 50 |

- Ferro has about 3 x the lit pixels of the face. At 0.55 x the current, the panel puts out roughly what the face does now, so he doesn't turn into a desk lamp.
- **Ramp, never step.** Change the register one step a frame through the enter/exit (§6). A brightness jump on a mostly white screen is very visible.

### 3.3 Glare and the translucent shell

- A full white OLED throws white into the shell around its window. The shell's colour has to stay the LEDs'; the screen should read as a lit window, not a floodlight. That's the other reason for the brightness drop.
- If the OLED sits behind translucent plastic, the white diffuses and eats the black edges. Then the +1 px rule becomes +2, and the glint probably won't survive (open question 2).

### 3.4 Power

Not a constraint. A 1.3" 128 x 64 PMOLED at full white is roughly 25 to 40 mA at 3.3 V at high brightness, and less at the ferro setting (measure it). The face is under about 10 mA. The strip is capped at 450 mA, so even full white is under a tenth of the strip's budget.

### 3.5 Burn-in and uneven wear

OLED pixels age with on-time, and aged pixels get dimmer.

1. **Ferro wears the panel about 3 x as fast as the face.** Fine as a visit (MS's "auto" mode is a ~60 s visit every 8 to 15 min). If he's left in ferro all day, see open question 4.
2. **White is the screen that shows old burn-in.** The face's eyes sit in about the same place most of the time, so on a long-used unit those areas have aged most and will show as two faint darker ovals on the white field. Ferro can't fix that; the face's own drift and tilts slow it down. Worth one look on the oldest vizMini.
3. **Keep ferro's own wear even.** He must not park: MS's crawl, stances and roaming already spread the black. No static black anywhere except the corner mask: no clock, no text, no floor line.

### 3.6 PMOLED row banding (bench check)

On passive-matrix panels like the SH1106, a row with many lit pixels can come out slightly dimmer than a row with fewer (the shared row driver sags under load). On a white field with a black drop, that would show as faint brighter bands to the left and right of him. Check at the ferro setting. If it shows, lower the brightness further; the sag scales with current.

### 3.7 Filings (MS's snow replacement)

MS replaces snow with 10 falling black "filings" that get pulled into him. Look:
- **2 px dashes, never 1 px specks.** One black pixel on white vanishes into the bloom or reads as a dead pixel.
- Vertical while falling. Inside 12 px of the rim they **turn to point at him** (horizontal, vertical or diagonal pair), lining up with the field the way filings do, then vanish into the rim.
- ≤ 10 at once, never inside the 2 px edge margin.

### 3.8 Say bubble (MS §6 asks)

Inverted: **a black box with white text**, 2 px corner radius, 2 px padding, no tail, in the band furthest from him (top band when he's on the floor). He answers it with a small 3-spike pulse toward it. It's static black for up to `SAY_DEFAULT_MS` (4 s), which is fine for wear.

---

## 4. The inside LEDs

### 4.1 The rule: in ferro mode the lights carry the feeling

The blob has no face most of the time, so the mood goes into the light. The screen is black and white; the shell is the colour.

### 4.2 Base: the mood table, clamped for a white screen

With the LED mode on **mood**, keep following `ledMoods[]` by expression (MS's moods already pick his temperament the same way), with these ferro clamps:

| Clamp | Value | Why |
|---|---|---|
| Saturation | ≥ 220 | white and pastel LEDs blend into the screen's white glow; deep colour around a white window is the look |
| Value | ≤ 150 before the global brightness | the shell glows; the screen stays the brightest thing in it |
| Speed | ≤ 6 | the blob is the motion; a fast light show fights it |
| `LM_STROBE` | becomes `LM_PULSE` | a strobing shell next to a full white panel is harsh |
| `LM_RAINBOW` | speed ≤ 3 | |
| White moods (sat 0: SURPRISED, SHOCKED) | become their hue2 at sat 220 | white belongs to the screen |

**Ambient** and **off** modes are unchanged; Kevin made ambient the independent show.

### 4.3 Cues (over the mood base)

MS §3.4 defines the cue layer (pop flash, swell, chase, split, ends, spark). Look rules on top:

1. **Mirror the strip when the pattern is symmetric.** Both LEDs of each mirror pair match (pairs depend on the strip geometry, MS question 4). Chase and Split are the only asymmetric cues.
2. **White only as a flash:** ≤ 150 ms, at the enter landing (MS "lights on") and on a web Boop. Never held.
3. **Spark (rainbow) only for finales** (Grand Crown, Twins merge, end of a show), as MS has it.
4. **Black on black on exit.** As his black fills the screen, the strip's value falls with the black share, down to 20 % at full black, then comes back with the face. When he swallows the light, the shell goes dark with him. (Kevin's "black-on-black on dark scenes", in light form.)
5. **Nothing on the strip changes faster than the blob does.** The pop flash rides the pop; it doesn't add its own beats.

---

## 5. Eyes: yes, as punctuation (MS says none: open question 1)

### 5.1 Against

- At 2 to 3 px they're the only white-inside-black detail, so the eye goes straight to them. Left on, they turn every pose into a face and the tentacles into arms: a bug, a crab or a soot sprite, not a field.
- Small features on a moving body shimmer unless they hold whole-pixel positions.
- With eyes up, the glint has to go.

### 5.2 For

- Two dots in a blob are read as a face instantly. It's the cheapest identity there is; without it ferro mode could be any screensaver.
- A Boop or a show ending wants a reaction that's about *him*.
- It keeps the family: the full-size ferro has eyes inside the drop.
- White on black is where tiny works: lit pixels bloom, so a 2 x 3 dot reads clearly at desk distance.

### 5.3 Verdict and spec

**Yes, as rare beats: visible ≤ 10 % of the time, never at the peak of a crown, reach or stack.** This matches MS's own question 3 option.

| Beat | Eyes |
|---|---|
| Landing after enter | open, one blink, then they sink away (§6.1) |
| Start of exit | open and look at the viewer, then grow into the real face (§6.2) |
| Web Boop | surprised, 600 ms, a blink, gone |
| End of a show or a big sequence | happy, 500 ms |
| Peekaboo (SHY) | open on the half of him still on screen, look toward the room, gone |

```
open (2x3)          surprised (3x3)     happy (^ ^)         blink (2x1)
..##......##..      .###.....###.       ..##......##..      ..............
..##......##..      .###.....###.       .#..#....#..#.      ..............
..##......##..      .###.....###.       ..............      ..##......##..
```

- Centres 8 px apart (close-set reads young), 2 px above the body centre on the floor.
- Whole-pixel positions with hysteresis, riding the body centre with a short lag.
- ≥ 4 px of black around each eye; off at size S.
- No brows, no mouth (Kevin's party-mode call). Gaze is a 1 px shift.

M floor drop with eyes open (left) and happy (right), 1:1:

```
┌────────────────────────────────────────┐   ┌────────────────────────────────────────┐
│                                        │   │                                        │
│                                        │   │                                        │
│           ▄▄▄████████████▄▄▄           │   │           ▄▄▄████████████▄▄▄           │
│        ▄██████████████████████▄        │   │        ▄██████████████████████▄        │
│      ▄██████████████████████████▄      │   │      ▄██████████████████████████▄      │
│     ██████████████████████████████     │   │     ██████████████████████████████     │
│    ████████████████████████████████    │   │    ████████████████████████████████    │
│   ████████████  ██████  ████████████   │   │   ███████████▀▄▄▀████▀▄▄▀███████████   │
│   ████████████▄▄██████▄▄████████████   │   │   ██████████████████████████████████   │
│   ▀████████████████████████████████▀   │   │   ▀████████████████████████████████▀   │
│    ████████████████████████████████    │   │    ████████████████████████████████    │
│     ▀████████████████████████████▀     │   │     ▀████████████████████████████▀     │
│      ▀██████████████████████████▀      │   │      ▀██████████████████████████▀      │
│         ▀████████████████████▀         │   │         ▀████████████████████▀         │
│                                        │   │                                        │
└────────────────────────────────────────┘   └────────────────────────────────────────┘
```

---

## 6. Transition: the face's black is his body

MS §4 has the right idea, and it's the look this brief would have picked: **the black background of the face is the ferrofluid.** It contracts into the drop and swallows the eyes; on exit it swells back into the screen and the eyes come back. It also matches the full-size bot's iris enter. Look rules for it:

### 6.1 Enter

1. **The edge is liquid from frame one.** The shrinking black keeps a ≥ 2 px wobble (MS holds E at 0.8: good). A clean shrinking circle or rectangle reads as a UI iris.
2. **Corners first, then the sides** (MS: agreed). On a 2:1 screen the sides travel furthest, so the liquid visibly gathers along the wide axis.
3. **Brightness follows the progress spring:** register = lerp(face, ferro, P), one write a frame. At MS's swap frame the screen is the face's blink frame (mostly black), so nothing jumps.
4. **Eyes:** with eye beats off, MS's version stands (they blink shut at P 0.45 and stop drawing at P 0.6). With eye beats on, they stay shut and stop drawing at P 0.6 the same way, then the 2 x 3 dots open on the landing, blink once and sink after about 600 ms. The glint fades in 200 ms after them.
5. **LEDs** cross-fade from the face mood to the clamped ferro base across P, with MS's white "lights on" flash at the landing.

### 6.2 Exit

1. With eye beats on, the dots open during MS's gather (0 to 250 ms) and look at the viewer, then shut. MS's full eyes then reopen on their blink as the face scale grows back.
2. The black swells past full screen, so **the white corners go last**: the mirror of the enter.
3. Brightness ramps back to the face value with P. The strip dims with the black share (§4.3 rule 4), then returns with the face.

### 6.3 Rejected

- **Hardware invert** (SH1106 0xA6/0xA7): a one-frame jump from 30 % to 90 % lit. Harsh, and not liquid.
- **Wipes and dither dissolves:** a UI transition, and dither swims.
- **Eyes flood the screen** (the white eyes grow into the white field): the black ends up at the edges, not the middle, so the drop has to come from nowhere, and the panel slams to full white in a couple of hundred ms.

---

## 7. Cost of the look

Fill the 1 KB buffer white, fill one black union of spans (MS sizes this), then a handful of fixed pixels: glint about 12, eyes ≤ 18, corner mask 20, filings ≤ 20, Field Lines ≤ 120. Droplets are sprite stamps. No per-pixel shading, no dither, no rotation blit. The look adds well under a millisecond on top of MS's estimate.

---

## 8. Do / don't

| Do | Don't |
|---|---|
| One medium drop (M, r 15) | A small blob with lots of little bits |
| ≤ 5 tall spikes per crown, ≥ 30° apart | 7 to 9 tall spikes in a crown: they fuse into a stepped lump |
| 2 px spike tips, plump bullet profile | 1 px points, needles, saw blades |
| Round 7 to 8 px bulbs on 4 to 5 px necks | Pointed or bulbless reachers, hooks, forks |
| Hand-drawn drop sprites, 5 px and up | Computed 3 to 4 px circles; drops that shrink to nothing |
| Mirror-symmetric fields | Random angles, which read as dirt at this size |
| One white crescent glint at r − 4.5, hidden while the eyes are up | A white dot glint, or a glint on tentacles or drops |
| Pure white field, rounded corners, panel brightness down | Patterns, borders, floor lines, dither, any static black |
| Saturated mood colour in the shell | White or pastel LEDs, strobes next to the white panel |
| Eyes as rare beats | Eyes on all the time |

---

## 9. Bench checks

1. **Calm edge:** 30 s at rest at 60 cm. The rim moves as one shape; no single pixels sparkle along it.
2. **Thin parts:** in slow motion, no tip, neck or bridge blinks out or breaks, and no 1 px slit opens between parts.
3. **Glint A/B:** on vs off. It should read as "wet", not as a hole or an eye. If it reads as a hole, drop it.
4. **Panel:** full white at the ferro setting on the oldest unit. Look for row banding beside him (§3.6) and ghost eyes (§3.5).
5. **Shell:** with the screen white, the LED colour still owns the shell, and the screen reads as a window, not a floodlight.

---

## 10. Open questions for Kevin

1. **Eyes** (also MS question 3): rare 2 x 3 beats as in §5 (recommended), or strictly none?
2. **The window:** is the OLED behind an open cutout, clear plastic, or the translucent body plastic? And is the panel pure white (not blue or yellow/blue)? Translucent plastic means +1 px on every minimum and probably no glint; a coloured panel changes the LED pairing.
3. **LED colour in ferro:** the mood table with the clamps in §4.2 (recommended), or one fixed "magnet" palette (deep cobalt with a magenta accent)?
4. **Panel care if ferro is set to "on"** (not MS's "auto"): dim to 50 after 5 min with nothing happening and go back to the face after 20 min? Or let it run until you change it.

---

## 11. Where this brief and the motion spec differ

| Topic | MS | This brief | Suggested resolution |
|---|---|---|---|
| Crown spacing | slots 17° apart, Rosensweig Crown 7 spikes, Grand Crown 9 | spikes ≥ 7 px need ≥ 30° (every other slot); ≤ 5 tall per crown | tall spikes on even slots only; the odd slots carry only ≤ 6 px spikes (the "Nine" shoulders, Spike Merge's small stage) |
| Glint radius | r − 3 | r − 4.5 | r − 4.5 (a 2 px rim reads as a crack) |
| Eyes | none | rare beats | Kevin, question 1 |
| Filings | "black specks" | 2 px dashes that turn to point at him | dashes |
| Wall Reach glass contact | bulb touches the glass | stops 2 px short, still flattened | 2 px gap, like the floor contact |
| Droplets | pool of 8 | ≤ 3 free; drop patterns (Rain, Pearl Chain) may use more | as this brief |
| White LED flash | enter landing | enter landing and web Boop, ≤ 150 ms | as this brief |
| Size, stances, floor contact, no floor line, enter/exit concept, rotation off | | agreed | |
