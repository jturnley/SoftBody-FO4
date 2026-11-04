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
`SoftBodyVaginaDeep` and `SoftBodyAnus`. The anal canal is SoftBody's own past its entrance: GT's is a
pocket 2.5 deep that only its anus ring moved, so it is stretched into a tube 8 deep with three rings
of four bones (`SB Anus{T,L,B,R}1-3`, at 2, 4 and 6 deep, each sprung to go straight outward up to
4) and a hidden pad tube at each ring (`SoftBodyAnalCanal`). So is the vaginal canal past its entrance:
GT's pocket hangs on two bones that only slide along it (its ring bones carry almost none of the
mesh), so it becomes a tube 10 deep from the slit, aimed at GT's deepest bone, with five rings of
twelve bones (`SB Vag1-5_0-11`, 3.6 to 9.2 deep), leaning 12 degrees toward her front past the
entrance (between a missionary and a doggy penis's line), and pads (`SoftBodyVaginalCanal`). A ring of
twelve more (`SB VagIn_0-11`) sits in GT's narrow entrance channel, 1.2 in, so the entrance closes
round a shaft as a collar does. As real entrances do, it sits at the back of the slit (GT's was near
its middle): the channel and the top of the canal are moved 1.2 back, easing out deeper in and into
the vestibule; the slit's edge doesn't move. The hood barely gives (1.5), the outer lips a little (3),
the rear lip 5; the skin round the openings follows the lip and anus bones within 1.25 of the edge.
GT's own canal pads are gone. The ring bones' XML is written from the NIF's bones. The pads follow the recipe of a TWB body whose lip and anus
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
- v0.6, measured in game with the plugin's log (doggy, vaginal): the physics opens the
  canal to about the penis's width (0.3 to 3-4 units across) and the labia from
  6 to 10. The skin around the openings now follows the lip bones (v0.1-v0.5
  barely moved it), and the entrance looked right in a first test. Team
  feedback pending.
- v0.7, measured in game (doggy, anal): the anus ring opens from about 1 to 3-4
  across, about the shaft's width, but the visible canal was a short narrow
  pocket the penis passed through. v0.8 replaces it with the deeper tube above.
- v0.11: the vaginal canal tube (above), after a look at how another FO4 body
  is built (a deeper canal, rings of bones, broad stretch weights; nothing taken
  from it). v0.12, from photos of real penetration: rings of twelve, the entrance
  ring at the back of the slit, a quiet hood and outer lips.
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
- optional: 4Stim, for the plugin's per-act physics and auto-alignment (Address
  Library for F4SE Plugins)

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

## The plugin (4Stim scenes)

`F4SE\Plugins\SoftBodyFO4.dll` (source in `plugin/`, built with xmake against 4Strands'
commonlibf4 and 4Stim's plugin interface) does what GT Softbody's scene scripts do, for 4Stim:

- **Physics by act.** For vaginal sex her body gets `SoftBody-CBBE-Vaginal.xml`, for anal
  `-Anal.xml`, otherwise the full `SoftBody-CBBE.xml` (swapped with FO4 FSMP's `DynamicHDT`). In
  each variant the opening not in use lets A-Body's penis (tag `Penis`) through, so it can't catch
  the penis or be dragged about by it. `build_prototype.py` writes the variants. The act comes from 4Stim's
  Scenes interface; a 4Stim build without it (the current one) gets it from where the penis goes:
  for 2.5 s after each scene change the plugin watches whether the penis tip reaches the vagina or
  the anus and which its line runs nearer.
- **Auto-alignment.** GT lets a bendable penis be steered into the opening; 4Stim's is rigid, so
  the plugin corrects his 4Stim alignment offset instead. It measures, over a couple of seconds of
  the loop, where the penis line crosses the opening in use (vaginal or anal sex), moves him, and
  measures again until it is within `fAlignTolerance`. 4Stim saves the offset with the scene, so
  a scene is aligned once. If the penis then runs at an angle to the canal (more than 6 degrees;
  missionary: about 20), it tries two steps of 4Stim's penis bend to learn its effect, sets the bend
  that lines it up, and centres the entrance again. Keys (game in front): **F11** align now, **Shift+F11** auto-align on/off,
  **Ctrl+F11** back to the scene's own alignment.
- **Tuning log.** With `bLogSamples=1` it logs her genital bones and the penis every 25 ms to
  `Documents\My Games\Fallout4\F4SE\SoftBodyFO4.log` (what `probe/` did).

Settings: `F4SE\Plugins\SoftBodyFO4.ini`. Needs Address Library for F4SE Plugins; without 4Stim
it does nothing.

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
