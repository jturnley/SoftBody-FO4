# SoftBody FO4 (prototype)

Port of GT Softbody (Skyrim SE, Nexus 152103, by Goutou) to the Fallout 4 CBBE
body on top of FO4 Faster HDT-SMP. Conversion is permitted with credit to the
original author; no paid/paywalled distribution.

## Stage 1 (this build)

Breasts, butt and belly react to the actor's hands: whole-region push plus
jiggle, using the bones CBBE is already weighted to (no reweighting).

| File | Purpose |
|---|---|
| `mod/meshes/SoftBodyFO4/SoftBody-CBBE.xml` | Body physics: dynamic `<bone>_SB` children of `L/RBreast_skin`, `L/RButtFat_skin`, `Belly_skin`, sprung to their parents, collision surface tag `SB_Body` |
| `mod/meshes/SoftBodyFO4/SoftBody-Hands.xml` | Kinematic hand collider, tag `SB_Hands` |
| `mod/meshes/.../FemaleHands.nif` | CBBE hands + SMP link (generated) |
| `mod/Tools/BodySlide/...` | BodySlide set **CBBE Body SoftBody** (group CBBE): CBBE body + SMP link (generated) |
| `tools/nif_smp.py` | Minimal FO4 NIF reader; attaches `HDT Skinned Mesh Physics Object` extra data |
| `tools/build_prototype.py` | Regenerates the `mod/` NIFs and BodySlide files from the installed CBBE |
| `tools/package.py` | Packs `mod/` into an MO2 install zip in `dist/` |

FO4 Faster HDT-SMP only simulates bones that are not in the actor skeleton, so
`build_prototype.py` moves each soft region's skin onto a new `<bone>_SB` child
node (same transform, weights untouched), like TWB and A-Body do. An SMP XML
must also name at least one skin bone of its mesh or no system is built.

FO4 renames a single-shape armor's geometry to `<ARMA> (id)[sex]/<ARMO> (id)`
when it attaches it, which breaks SMP mesh descriptors (they match on shape
name). `build_prototype.py` therefore adds a culled one-triangle shape,
`SoftBodyFO4NameAnchor`, to the body and hands NIFs; with two geometries FO4
keeps the NIF names (`CBBE`, `BaseFemaleHands3rd:0`) on every actor and skin.
Runtime names show up in the SMP log's actor-skeleton dump.

Local dents: `tools/dents.py` clusters each soft region's vertices (in the
region bone's bind space) into dent bones `<bone>_SBnn`, children of
`<bone>_SB`, and spreads the region weight over the nearest three. CBBE
breasts are at most ~46% `LBreast_skin` and the rest `Chest_skin`, so half of
the breast-core `Chest_skin` weight is moved onto the dents too (`DENTS` in
`build_prototype.py`). Each dent bone hangs off its region bone on a short
stiff spring (`dentSpring`). The physics XML sources live in `src/`; the
build writes the game copies with the dent bones filled in.

Genitals (`tools/genitals.py`, `tools/genital_textures.py`, `tools/sse_nif.py`): GT Softbody's
own vulva/vaginal canal ("2Vagina"), anal canal ("2Anus") and the sculpted body around them are
read from GT's Skyrim SE BodySlide NIF, fitted onto CBBE's crotch (ICP with scale; ~1.002, 2.4 deg,
residual median 0.3), and stitched into a hole cut in CBBE (triangles only, so CBBE's .osd stays
valid). Two new shapes: `SoftBodyVulva` (skin material; UVs, body weights and slider offsets from
the CBBE surface under it) and `SoftBodyGenitals` (GT's vagina texture, its model-space normal map
re-baked to tangent space; material made from the game's own skin .bgsm). GT's 22 genital bones
sit under `Pelvis_skin`. Each new shape gets its own copy of the shader block: two drawn shapes
sharing one hang the game when the body is put on. Collision for the openings is GT's own: the pads
of its HVA collision NIF (one per genital bone) become the hidden shapes `SoftBodyVaginaOut`,
`SoftBodyVaginaDeep` and `SoftBodyAnus`. They follow the recipe of a TWB body whose lip and anus
proxies do collide in FO4: per-vertex shapes (spheres), weighted wholly to their genital bones,
colliding with everything except each other, hidden by having no shader. GT's per-triangle pads
(v0.1–v0.2) built in SMP but never collided with A-Body's penis, which is itself per-triangle in
4Stim scenes (`MaleBodyHard.xml`). The genital bones carry GT's springs
(ported from its `FemaleCollisionHandVaginaAnal.xml`): GT's Skyrim anchor bones become kinematic
NIF-only nodes turned as in GT, since a spring's limits run along its anchor's axes. Their travel
is capped at 3 units (GT allows up to 30): in scenes 4Stim makes A-Body's penis kinematic, and one
off the opening dragged her flesh out that far. The drawn shapes (`SoftBodyVulva`,
`SoftBodyGenitals`) are `BSSubIndexTriShape`s with CBBE's four segments, as body parts are, so
that they take the NPC's skin tint. Slider data for the new shapes: `SoftBodyGenitals.osd`. GT's licence
allows conversion with credit to Goutou, no paid use. Needs GT Softbody installed (Skyrim side)
when building.

Bone `<shape>`s never collide in FSMP (bones are added to Bullet with
group/mask 0); only mesh shapes do.

Status (2026-11-01):
- Confirmed in game: jiggle and hand-to-body push on the player and NPCs, local
  dents, the transplanted genitals (undressing works), and vaginal and anal
  collision with A-Body in 4Stim scenes (v0.3).
- v0.6, measured in game with `probe/` (doggy, vaginal): the physics opens the
  canal to about the penis's width (0.3 to 3-4 units across) and the labia from
  6 to 10. The skin around the openings now follows the lip bones (v0.1-v0.5
  barely moved it), and the entrance looked right in a first test. Team
  feedback pending.
- v0.4: the genitals keep their own texture. v0.1-v0.3 shipped their material
  with the skin material's SkinTint flag and root template, so in game FO4
  dressed them in the actor's skin texture. Without the flag they don't take
  the NPC's skin tint either; their rim is matched to the default skin.
- Built but not yet tested in game: the genital springs (v0.1 tore the vulva
  out under a kinematic penis; v0.2 caps their travel), the skin seam fix
  (the patch reads the skin's normal map in CBBE's own tangent frames), and
  skin tint on the vulva and genitals (v0.2; v0.1 left them pale on
  dark-skinned NPCs).
- Not yet done: penetration testing (blocked by 4Stim leaving the A-Body penis
  flaccid), arm and thigh colliders, clothing, animation-framework hooks.

## Requirements

- Fallout 4 with F4SE
- CBBE and BodySlide
- FO4 Faster HDT-SMP
- a female skeleton with SMP support (tested with DFS and Skeletal
  Adjustments for CBBE)
- optional: A-Body (male), whose `VirtualPenis` collides with the openings

## Install / test

1. Install the zip from the latest
   [release](https://github.com/jturnley/SoftBody-FO4/releases) with MO2. It must win conflicts
   with CBBE and with any other SMP body (for example TWB SMP). It ships its own
   `FemaleHands.nif`, which overrides other hands mods.
2. Open BodySlide, pick **CBBE Body SoftBody** (group CBBE), choose your
   preset, and Build (tick Build Morphs for LooksMenu). This replaces the
   `FemaleBody.nif` you built before.
3. In game, on a nude female actor: watch idle/run jiggle, then bring a hand to
   the chest (e.g. a pose or animation that touches the body).
4. Debug: set `enableBulletVisualization` to `true` in
   `F4SE\Plugins\FO4FasterHdtSMP\configs.xml` and restart to see the collision
   shapes. `logLevel` 3+ there gives more detail in the F4SE log.

## Opening logger (temporary)

`src/debug/SoftBodyOpeningLog.psc` logs the openings during a 4Stim scene, for
tuning. It needs Garden of Eden Papyrus Script Extender and 4Stim, and isn't in
the release zips. Compile it from its own folder with the CK's
PapyrusCompiler.exe (import the game's base sources, Garden of Eden's and
4Stim's) and put the `.pex` in `Scripts\`. Then in the console, before or
during a scene:

```
cgf "SoftBodyOpeningLog.Run"
```

About twice a second, it writes the opening widths, each genital bone's distance
from its anchor, and the penis tip's distances to the openings to
`Data\F4SE\SoftBodyOpenings.ini` (MO2: `overwrite\F4SE`).

## What to report back

- Does the body load at all (no invisible / exploded body)? An invisible or
  stretched body means an XML bone/shape name was rejected; check the log.
- Do breasts/butt jiggle when running and settle when idle?
- Does a hand visibly push the breast? Too weak/strong, too slow to return?
- Any constant twitching at rest (usually limits or stiffness too high).

## Rebuild

Needs CBBE (FO4) and GT Softbody (Skyrim SE) installed; the paths are the
arguments of `build_prototype.py` (see its top).

```
python -I tools/build_prototype.py
python -I tools/package.py
```
The first rebuilds `mod/`; the second packs it into an MO2 install zip,
`dist/SoftBodyFO4-<date>-<commit>.zip`. Commit before packaging, so the zip
names the commit it was built from. With `--release <tag>` it also publishes
the zip as a GitHub pre-release of HEAD (push first); the notes list the
commits since the previous release.

## Credits

- GT Softbody (Skyrim SE) by Goutou: soft-body design, genital geometry,
  textures, collision pads and springs. Conversion is permitted with credit;
  no paid use.
- CBBE by Ousnius and the CBBE team: the body, its BodySlide set and its
  slider data. Conversion permission was obtained by the 4Stim project lead.
- FO4 Faster HDT-SMP; FSMP (GPL3) source used as the reference for SMP
  semantics.
