# NEON COIL — art asset pack

Everything you need to generate the game's art in Google AI Studio: which model to use
for what, the shared style block, and a copy-paste prompt per asset.

**The game needs none of this to run.** All gameplay visuals are procedural (see
"Rendering" and "No assets" in the README). This pack covers the store page, the launch
window icon, and the small number of illustrations that genuinely improve the front end.

---

## 1. Which model

The picker has four. They are not interchangeable — pick by *job*, not by price.

| Model | Id | Use it for | Cost / image |
|---|---|---|---|
| **Nano Banana Pro** | `gemini-3-pro-image` | **The logo, the wordmark, and every Steam capsule.** Anything with lettering or that a buyer judges you on. | $0.134 |
| **Nano Banana 2** | `gemini-3.1-flash-image` | Snake portraits, background plates, marketing scenes. The workhorse. | $0.0672 |
| **Nano Banana 2 Lite** | `gemini-3.1-flash-lite-image` | Exploring composition — fire ten variants, pick one, re-render it on Pro. | $0.0336 |
| Nano Banana | `gemini-2.5-flash-image` | Skip it. Older generation, costs more than Lite and is beaten by NB2. | $0.039 |

**Use Pro wherever text appears.** Accurate in-image lettering is the single biggest
gap between generations, and it is exactly what failed on the free generator earlier —
it returned a garbled fake wordmark. A Steam capsule with mangled letters is unusable.

**Suggested workflow:** rough on Lite → settle composition → final on NB2 → anything
with type or going on the store page, redo on Pro.

**Total cost** for this whole pack on the recommended split is roughly **$2–3**. Do not
economise here; regenerating a capsule twice costs less than a coffee.

### One prompting difference that matters

The earlier free generator was CLIP-style and needed *short* prompts. The Gemini image
models are instruction-following — **longer, more specific prompts work better.** State
composition, lighting, palette and negatives explicitly. Every prompt below is written
for that.

---

## 2. The palette — non-negotiable

Taken from `src/core/Colors.h`. Quote the relevant hex values in every prompt; that is
what keeps generated art matching the running game.

| Role | Hex | Role | Hex |
|---|---|---|---|
| Void black | `#0d0f14` | Arena floor navy | `#141c2b` |
| Wall slate | `#3a4761` | Silver text | `#9aa5b1` |
| **Viper** emerald | `#3ddc84` | **Bulwark** cobalt | `#5b8cff` |
| **Wraith** aqua | `#7ff0d8` | **Midas** gold | `#ffe066` |
| **Ouroboros** orchid | `#d972ff` | Food coral | `#ff7a5c` |
| Hazard red | `#ff5555` | Lime | `#a6ff6a` |

### Style block — append to every prompt

> **STYLE:** Neon arcade game art for a strictly 2D top-down grid game — a classic
> flat Snake game, the same visual class as Pac-Man or the original Nokia Snake, NOT
> an isometric or 3D-rendered game. Orthographic top-down camera, dead straight down,
> zero perspective, zero camera tilt, zero depth. Flat graphic shading with hard-edged
> flat colour fills, not painterly rendering. Strong emissive rim light and a soft
> additive bloom around anything glowing. Dark near-black `#0d0f14` to deep navy
> `#141c2b` backgrounds. Restrict the palette to: `#0d0f14 #141c2b #3a4761 #9aa5b1
> #3ddc84 #5b8cff #7ff0d8 #ffe066 #d972ff #ff7a5c #ff5555 #a6ff6a`. The snake is always
> built from discrete square segments with small visible gaps between them — never a
> smooth organic tube.
> **NEGATIVE:** no isometric view, no 3D render, no perspective, no camera tilt, no
> depth-of-field, no drop shadows implying height, no text, no letters, no numbers, no
> watermark, no signature, no UI chrome, no photorealism, no muddy gradients, no
> realistic snake scales.

Two clauses matter most. Without the isometric/3D negative, every model defaults to a
tilted "mobile game" render — wrong game entirely. Without the square-segment clause,
every model draws a realistic organic snake.

---

## 3. Logo and wordmark — Nano Banana Pro

The game already renders `NEON COIL` in its own 5×5 block font. That letterform **is**
the brand — a store logo in some unrelated typeface will look like a different product.

Two ways to do this, best first:

### 3a. Reference-guided (recommended)

Export the in-game title, upload it to AI Studio as an image input, and ask Pro to
treat it rather than invent it:

```powershell
.\build\bin\NeonCoil.exe --screenshot menu docs\ref_wordmark.png --frames 1
```

Then, with `ref_wordmark.png` attached:

> Using the attached image as the exact letterform reference, redraw only the words at
> the top as a polished game logo. Keep the blocky pixel-grid letter shapes exactly as
> they are — same proportions, same square corners, same spacing. Render them as
> glowing neon tubes in emerald `#3ddc84` with a bright white-hot core, a soft outer
> bloom, and a faint reflection on a dark floor beneath. Transparent or pure black
> `#0d0f14` background. Nothing else in frame — no snake, no UI, no extra words.
> Aspect ratio 16:9, 1280×720.

### 3b. From scratch

> A video game logo wordmark reading exactly "NEON COIL", on two stacked lines: "NEON"
> above, "COIL" below, both centred. Chunky 1980s arcade pixel-block letterforms with
> hard square corners, no anti-aliasing on the letter edges. The letters are glowing
> neon tubes: emerald `#3ddc84` outer glow, bright white-hot `#f2f5f7` core. A single
> thin coiled snake made of square segments loops around the letters without obscuring
> them. Pure black `#0d0f14` background. Spelling must be exactly N-E-O-N C-O-I-L.
> Aspect ratio 16:9, 1280×720.

**Always verify the spelling by eye.** If it is wrong, regenerate rather than patch.

Steam's `library logo` slot wants this on transparency at 1280×720. Generate on pure
black and key it out, or ask Pro for a transparent background and check the alpha.

---

## 4. The roster — Nano Banana 2

Five portraits. **These can go in the game**, in the FIELD REPORT panel of the menu.

That works because a portrait shows the *type's* signature colour, not the player's
chosen colour — five fixed images, not forty. The animated preview strip beside it
stays procedural and follows the player's colour pick.

Generate all five **in one session** so lighting and framing stay consistent. Keep the
framing clause identical; change only the bracketed part.

**Framing clause (same every time):**

> Strict flat top-down portrait, camera dead straight down, zero perspective, of a
> stylised serpent built from discrete square segments, coiled in an S curve, centred,
> filling the frame. Plain deep navy `#141c2b` background with a soft radial glow
> behind the snake in its own colour. Identical framing, camera and lighting across the
> set. Aspect ratio 3:4, 768×1024. The serpent is **[VARIANT]**.

| Snake | `[VARIANT]` |
|---|---|
| **Viper** | *slender and fast, bright emerald `#3ddc84`, glossy segments with a sharp white highlight along the top edge, motion streaks trailing behind the tail* |
| **Bulwark** | *thick and heavily armoured, cobalt blue `#5b8cff`, overlapping riveted metal plate segments, a faint hexagonal shield barrier shimmering around it* |
| **Wraith** | *semi-transparent and ghostly, pale aqua `#7ff0d8`, the tail dissolving into mist, the floor grid faintly visible through its body* |
| **Midas** | *gilded and ornate, gold `#ffe066`, faceted gem-like segments catching light, small gold sparks drifting upward* |
| **Ouroboros** | *banded orchid purple `#d972ff`, alternating light and dark segments, curled into a near-complete ring so its head almost meets its tail* |

Save as `assets/portraits/snake_<name>.png`, lowercase. That is the path the menu will
look for once wired up.

---

## 5. Backgrounds — Nano Banana 2

These sit *behind* UI, so they must be quiet. Low contrast, heavy vignette, nothing
competing with text.

**Menu background plate** — `1920×1080`:

> A vast flat 2D top-down arena floor, camera dead straight down with zero
> perspective, tiled in a faint glowing grid pattern fading to black at the edges of
> the frame — the fade is pure darkness, not distance, the floor plane stays flat and
> does not recede or tilt. Scattered dim slate `#3a4761` obstacle blocks scattered flat
> across the floor. Soft volumetric neon glow spilling in from off-frame. Very low
> contrast, heavy vignette, deliberately empty and uncluttered in the centre and lower
> two thirds so interface elements can sit on top. Nothing in sharp focus.
> Aspect ratio 16:9.

**Steam page background** — `1438×810`: same prompt, add *"even darker and more
desaturated, almost monochrome navy"*.

---

## 6. Board objects — Nano Banana 2 (or Lite while exploring)

Food, walls, hazards, the shield effect. Illustrative reference for the store page and
`docs/`, not something the game loads — see §10 for why gameplay art stays procedural.
Generate on Lite first since these are simple enough that composition rarely needs a
Pro redo; move to NB2 only if Lite's result looks flat or muddy.

Same rule as everywhere else: **flat top-down object icon, no isometric angle, no
tilt.** Each one below is written as a full standalone prompt (style block folded in)
so any single row can be pasted alone.

| Object | Prompt |
|---|---|
| **Normal food** | *Flat top-down game icon of a single round glowing orb, coral `#ff7a5c`, with a bright white specular highlight near the upper left and a soft outer glow. Centred, filling the frame, plain `#0d0f14` background, zero perspective, orthographic camera straight down. No text.* |
| **Bonus food** | *Flat top-down game icon of a four-pointed star-shaped gem, gold `#ffe066`, faceted flat-shaded facets, radiating four short light spikes from its points, soft outer glow. Centred, filling the frame, plain `#0d0f14` background, zero perspective, orthographic camera straight down. No text.* |
| **Wall / obstacle tile** | *Flat top-down game tile of a solid square block, slate `#3a4761`, with a lighter `#4d5c7a` flat highlight band along the top edge and a darker flat shadow band along the bottom edge — flat 2D bevel shading only, no 3D extrusion or height. Centred, filling the frame, orthographic camera straight down, edge-to-edge with no margin. No text.* |
| **Border wall tile** | *Flat top-down game tile of a solid square block of dark blue industrial metal `#5b8cff` on near-black `#0d0f14`, riveted corners, a thin glowing seam down the centre. Flat shading, no 3D extrusion. Centred, filling the frame, orthographic camera straight down, edge-to-edge. No text.* |
| **Sentinel hazard** | *Flat top-down game icon of a diamond-shaped drone, hazard red `#ff5555`, sharp angular flat-shaded plating, one glowing white eye at its centre. Centred, filling the frame, plain `#0d0f14` background, zero perspective, orthographic camera straight down. No text.* |
| **Shield effect (Iron Scales)** | *Flat top-down game icon of a translucent hexagonal energy barrier ring, cobalt `#5b8cff`, glowing edges, hollow centre, small arcs of electricity along the rim. Centred, filling the frame, plain `#0d0f14` background, zero perspective, orthographic camera straight down. No text.* |

---

## 7. Steam capsules — Nano Banana Pro

Exact sizes Steam requires. Generate the art **without lettering**, then composite the
logo from §3 over it in any editor — that guarantees correct branding on every size and
lets you reposition per aspect ratio.

| Steam slot | Size | Prompt |
|---|---|---|
| **Main capsule** | 1232×706 | *Hero shot: a glowing emerald `#3ddc84` square-segmented serpent coiled into a tight spiral, seen from directly above, centred on a dark navy `#141c2b` tiled arena floor. Concentric glowing wall rings around it. A single coral `#ff7a5c` orb glowing nearby. Dramatic bloom. Leave the upper third clear and uncluttered for a logo.* |
| **Header capsule** | 920×430 | *Wide composition, strict flat top-down camera: an emerald `#3ddc84` square-segmented serpent head entering from the left, a glowing coral `#ff7a5c` orb on the right, dark tiled arena floor between them. Leave the centre open for a logo.* |
| **Small capsule** | 462×174 | Same as header, add *"extremely simple, readable as a thumbnail at 200 pixels wide, one clear focal shape".* |
| **Vertical capsule** | 748×896 | *Tall poster: a glowing emerald `#3ddc84` square-segmented serpent rising vertically through a dark neon arena of glowing blocks, viewed top-down. Leave the top quarter clear for a logo.* |
| **Library capsule** | 600×900 | Same as vertical, reframed portrait. |
| **Library hero** | 3840×1240 | *Ultra-wide banner: the arena stretching across the full frame, an emerald serpent coiling through the centre-left, deep negative space on the right. Very dark, cinematic, heavy vignette. Generate at the highest resolution available and upscale if needed.* |

Screenshots for the store are **not** generated — take them from the real game:

```powershell
.\build\bin\NeonCoil.exe --screenshot play docs\shot_play.png --frames 400
```

---

## 8. Window icon — Nano Banana Pro

> Bold minimal app icon: a single emerald `#3ddc84` snake made of square segments,
> coiled into a thick spiral ring, glowing, centred on a near-black `#0d0f14` rounded
> square. Heavy simple silhouette that stays readable at 32 pixels. No text.
> Aspect ratio 1:1, 1024×1024.

Downscale to a multi-size `.ico` (256/128/64/48/32/16) for the executable.

---

## 9. Consistency rules

- One generation session per set. Across sessions the lighting drifts and the set stops
  looking like one game.
- Same light direction throughout: **top-left**.
- Outlines on all sprites or none. Pick one, keep it.
- No text anywhere except the logo in §3. Everything else gets type composited on top.
- The palette is closed. If a result introduces a colour that is not in §2, regenerate.
- Reject anything with a smooth organic snake body. Segments, always.

## 10. What stays procedural, and why

Do not generate in-game snake, food, wall or particle sprites.

The player picks one of **eight** colours across **five** types, and phase, dash and
shield recolour the snake on top of that. Baked sprites would need forty variants and
still could not tint at runtime. The procedural shapes stay crisp at 4K, tint for free,
and carry no licensing question.

The only art that belongs inside the game is the five roster portraits (§4), the menu
background plate (§5) and the window icon (§7) — all of which are fixed, and none of
which need to follow the player's colour.

## 11. Where files go

```
assets/
  portraits/     snake_viper.png  snake_bulwark.png  snake_wraith.png
                 snake_midas.png  snake_ouroboros.png
  ui/            menu_background.png  icon.png
  reference/     food_normal.png  food_bonus.png  wall.png  border.png
                 sentinel.png  shield.png                        (illustrative only)
  marketing/     logo.png  capsule_*.png  library_hero.png     (gitignored)
```

`assets/marketing/` is gitignored — store art is large and regenerable. `portraits/`
and `ui/` are shipped content and should be committed.

Drop the files in and say the word; wiring the portraits into the menu panel and the
background plate behind it is a small change, and the icon needs a Windows resource
script added to the CMake target.
