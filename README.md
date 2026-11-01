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
of its HVA collision NIF (closed, outward-facing, one per genital bone) become the hidden shapes
`SoftBodyVaginaOut`, `SoftBodyVaginaDeep` and `SoftBodyAnus`, per-triangle-shapes that collide with
the tag `Penis` (A-Body's VirtualPenis) and with the hands. The genital bones carry GT's springs
(ported from its `FemaleCollisionHandVaginaAnal.xml`): GT's Skyrim anchor bones become kinematic
NIF-only nodes turned as in GT, since a spring's limits run along its anchor's axes. Slider data for the new shapes: `SoftBodyGenitals.osd`. GT's licence
allows conversion with credit to Goutou, no paid use. Needs GT Softbody installed (Skyrim side)
when building.

Bone `<shape>`s never collide in FSMP (bones are added to Bullet with
group/mask 0); only mesh shapes do.

Status (2026-10-31): jiggle and hand-to-body push confirmed in game on the
player and NPCs (name-anchor build; BodySlide keeps the anchor shape).
Local dents (49 dent bones) working in game. Not yet: (needs extra bones + Outfit Studio weighting), arm/thigh
colliders, genitals, clothing, animation-framework hooks.

## Install / test

1. In MO2, enable **SoftBody FO4 Prototype** and make sure it is above
   (higher priority than) the CBBE mod. FO4 Faster HDT-SMP must be enabled.
2. Open BodySlide, pick **CBBE Body SoftBody**, choose your preset, Build.
   This replaces the FemaleBody.nif in Bodyslide_Output (currently True
   Wasteland Body SMP). To go back, rebuild the TWB set.
3. In game, on a nude female actor: watch idle/run jiggle, then bring a hand to
   the chest (e.g. a pose or animation that touches the body).
4. Debug: set `enableBulletVisualization` to `true` in
   `F4SE\Plugins\FO4FasterHdtSMP\configs.xml` and restart to see the collision
   shapes. `logLevel` 3+ there gives more detail in the F4SE log.

## What to report back

- Does the body load at all (no invisible / exploded body)? An invisible or
  stretched body means an XML bone/shape name was rejected; check the log.
- Do breasts/butt jiggle when running and settle when idle?
- Does a hand visibly push the breast? Too weak/strong, too slow to return?
- Any constant twitching at rest (usually limits or stiffness too high).

## Rebuild

```
python -I tools/build_prototype.py
```
then copy `mod/` over `E:\FO4-MO2\mods\SoftBody FO4 Prototype`.
