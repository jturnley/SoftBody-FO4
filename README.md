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
stiff spring (`dentSpring`). Since v0.32 each dent is also sprung (`dentLink`, softer) to its four
nearest dents, a lattice as in GT Softbody Ultra (by Goutou), so a press spreads into a smooth dimple
instead of one cluster's bump. The physics XML sources live in `src/`; the
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
  that lines it up, and centres the entrance again. Keys (game in front): **F11** steps through four modes, aligning at once: *auto* (the scene's act,
  or where the penis goes), *vagina*, *anus* (either also sets that act's physics) and *off*. The mode
  holds across scene changes. **Ctrl+F11** goes back to the scene's own alignment. `bAutoAlign=0` starts in *off*.
- **After every BodySlide build: `python -I tools/rebind_output.py <built FemaleBody.nif>`.** BodySlide
  moves vertices by the sliders but never moves bones, so a preset that reshapes the crotch (one tested:
  about 2 units) leaves the openings' skin off the bones that carry it, and the aim, rims, pads and
  canals all miss what is drawn. The tool moves every genital node by the sliders' displacement of the
  skin around it (against the reference mesh) and rebinds the skin, so the body looks the same at rest
  with its bones on the skin. It keeps the original as `.nif.bak`. (To do: let the plugin do this.)
- **Openings round the penis.** Left to collision, the rims opened where the thrusting penis shoved
  their pads: along the slit, lagging behind, oval (v0.14: the entrance 7.3 front to back, 3.5 across).
  So each visible hole has two rings of bones of our own: 24 at its edge (`SB VagRim_*`,
  `SB AnusRim_*`) and 12 further out (`SB VagRimO_*`, `SB AnusRimO_*`). The vagina also has its
  12-bone entrance ring inside. They're kinematic in her physics files, and every 4 ms the plugin moves
  them round the penis wherever the shaft or its rounded tip passes the opening.
  The rings' centre moves onto his line (up to `fRimFollow`, 1.5). The inner ring opens to his thickness
  there, from 2.2 at the root to 1.8 at the head, plus `fRimMargin`. The outer ring opens to where the
  skin between the two keeps its area, so it always stays outside the inner one.
  v0.20 instead pushed each bone out from his line on its own. He swings 10–20° through a stroke, which
  puts his line about a unit off the hole's centre, so both sides of the ring were pushed the same way
  and it shut. The vaginal slit's two ends fall on bones 0 and 12 of both rings, so the hole parts
  along it.
  The skin inside the inner ring rides on it, and between the rings on both by distance. Past the outer
  ring, out to 4–4.5 units and including CBBE's body, it rides on the outer ring by the share that keeps
  its area. The vulva meets CBBE only 1.7 units from the vagina's middle at the sides. v0.19 tore that
  seam, v0.20, holding it, stretched the skin, and v0.21's single ring of 12 showed as a polygon. GT's
  lip and anus bones are no longer driven, because moved on their own they folded the skin.
  Only the opening in use opens (the perineum is short: a penis in one passes the other's rim).
  `bDriveOpenings`, `fPenisRadius`, `fPenisRadiusTip`, `fPenisTipLength`, `fPenisTipExtra`,
  `fPenisBaseExtra`, `fRimSmoothing`, `fRimFollow`, `fRimCatch`, `fRimMargin` in the INI. The lips no
  longer jiggle or take a hand's push.
- **The snake aim (v0.31).** His shaft runs straight from its base to the entrance of the opening in
  use, then along her canal's middle (its rings' rest centres, to the cervix). Each of his skin bones,
  `Penis_02_SMP` to `Penis_05_SMP`, is placed at its own distance along that path and turned to it,
  easing in and out. Before his tip reaches the entrance, the path just points him at it. It gives up,
  leaving the animation's pose, when the entrance is more than `fBendMax` off his line or the canal
  turns more than `fSnakeMax` (60°) from it. The idea is fo4-ocbpc's (ReidenXerx, GPL-3); this is our
  own version, not its code. Before, the plugin only turned him toward the opening and had the canal
  chase his line, so at 22–49° off the canal it swung up to 4 units and tore.
  Since v0.35 the path is a smooth curve. As a polyline it turned all at once at the entrance and at
  each ring, and his shaft kinked there. It now leaves his base along his own line for a third of
  the way to the entrance, so the bend starts gently at his root, and corner cutting (Chaikin, four
  rounds) rounds every turn.
- **The penis bent through the opening** (before v0.31). Every 4 ms the plugin bends his penis so his line runs
  through the centre of the opening in use, by at most `fBendMax` (20°), following over
  `fBendSmoothing`. The bend is split evenly between his shaft's first two joints, `Penis_02_Offset`
  (at the base of his tracked line) and `Penis_03_Offset`. v0.22 turned him at his root
  (`Penis_00_Offset`). That tore the skin where his shaft meets his body into sheets, because that
  skin is weighted partly to the shaft and partly to `Penis_Offset_SMP`. At 15°, 222 edges of
  A-Body's mesh stretched more than 3×; split across the two shaft joints, no edge stretches past
  1.5× even at 20°. Each opening follows the piece of his curved shaft that passes it.
  v0.23 wrote those two joints, but something rewrites them every frame (16,000 times in one test), so
  the bend rarely showed. The plugin now turns the `_SMP` bones his skin hangs on (`Penis_02_SMP` to
  `Penis_05_SMP`) about those joints instead. Nothing animates them.
  With the bend on (`bBendPenis=1`), the aligner no longer tries 4Stim's bend steps.
- **The vaginal canal follows him.** The canal's six rings of 12 bones are the entrance (`SB VagIn_*`)
  and `SB Vag1_*` to `SB Vag5_*`, 3.6 to 9.2 deep. Each is now placed the way the openings are, on a
  centre of its own: where his line passes it, the ring moves right onto his line (up to
  `fCanalFollow`, 8: v0.28's 3 let a penis 25° off the canal run out through its wall) and opens to his thickness there, rounding off over his tip. Rings past his tip
  stay shut. Before, the deeper rings were springs on fixed bases that his collision pushed open, so
  they kept to the canal's own line while he ran through at 10–20° to it. They are kinematic now
  (`bDriveCanal`, `fCanalFollow`, `fCanalCatch` in the INI).
  The anal canal's three rings (`SB Anus{T,L,B,R}1` to `3`) are driven the same way since v0.29. They
  were springs that stayed shut, so its entrance stayed closed behind the anus ring, which followed him
  forward and pulled the skin into a sheet.
- **The cervix takes his tip.** The canal ends in a closed cap 0.8 units past its last ring, and the
  cap rides on that ring. Once his tip reaches the cap, the plugin carries the canal deeper along his
  line by however far he has gone past it, up to `fCervixStretch` (4). The push is shared out by depth,
  so the canal stretches evenly from the entrance (which stays put) to the cervix (which moves the full
  amount), and the cap stays capped over his tip. Before, his tip went through the cap. The tuning log
  records the push (`cervixPush`).
- **The cervix, os and uterus.** GT's canal ends closed. The mesh builder cuts the very tip off that end
  and carries the canal on as a shape of its own, `SoftBodyUterus`. It runs from the cervix's face,
  narrowing to the os (0.08 round), through a cervical canal 1 unit long, into a uterine cavity 3 long,
  1.2 round side to side and about half that front to back, closed at its top. The cut's edge is
  copied exactly, positions and weights both, so the two shapes meet with no seam. Its bones are a ring
  of 8 at the os (`SB Os_*`), a ring of 8 round the cavity (`SB Uterus_*`) and one at its top
  (`SB UterusTop`), all kinematic. The plugin carries them with the cervix push, opens the os and
  swells the cavity (`fUterusSwell`) when asked. Move the camera inside to see it.
- **An interface for other plugins.** `plugin/include/SoftBodyFO4API.h` describes three exported
  functions, for 4Strands' fluid:
  - `SoftBodyFO4_GetCanal` gives the followed woman's canal rings (centres and how far open),
    the cervix, the os, the uterus's centre, axis, length and width, the fill and the cervix push,
    all in world space.
  - `SoftBodyFO4_SetUterusFill` sets how full the uterus is (0 to 1), which swells it.
  - `SoftBodyFO4_SetOsOpen` sets how far the os opens.
- **The anus is aimed at its mouth, and painted there.** The anus's rim sits at the bottom of GT's
  pucker, 0.8 inside her skin along the canal. The aligner and the bend aimed his line through the
  rim's centre, so coming in at an angle he crossed her skin forward of the hole, on the funnel's side,
  where CBBE's skin also paints the anus (0.95 across the canal's line). Both now aim at where the
  canal's line meets her skin. Round the hole, the patch reads the skin texture 0.95 further along, so
  the painted anus sits on the hole (`ANUS_PAINT_SHIFT`, `ANUS_PAINT_R` in `tools/genitals.py`).
  In game he still met her forward of the hole, so `fAnusAimBack` (1.0) moves the anus's aim toward
  her back, across the anal canal.
- The anus is denser, with a folded pucker (v0.33; the method follows UBE 2.0's body, by Johnny
  Bungalow, studied for ideas only, nothing taken from it). GT's skin there had ~0.17-long triangle
  edges. Within 1.9 of the anus it is now subdivided once (`ANUS_DENSE`; never on the seam with the
  anal canal, and conforming, so there are no cracks). That gives ~0.09 edges, about UBE's density.
  Ten radial folds (`ANUS_FOLDS`, 0.05 deep, `ANUS_FOLD_R`) converge on the closed hole. Needs a
  BodySlide rebuild.
- Her body collides with his (v0.34; the method follows UBE 2.0's SMP collision objects, studied for
  ideas only). A low-poly copy of CBBE (cells 1.5 wide, `BODY_COL_CELL`) is split by region into
  hidden shapes: `SoftBodyCol{Breasts,Belly,Butt,Legs,Arms,Torso}`, about 7,000 triangles in all.
  Each is skinned like the skin it stands for, physics bones and dents included, so his A-Body
  Legs, Belly, Pecs, Arms, Hands and Penis press her breasts, belly and butt, and her thighs hold
  him. The shapes are tagged `SB_Col` and skip her own shapes and his balls. Nothing within 4.5 of
  the openings (`BODY_COL_CLEAR`), where the pads and rims are in charge. Needs a BodySlide rebuild.
  Since v0.36 the belly, butt and waist are one shape, `SoftBodyColTrunk` (`BODY_COL_JOIN`). As three,
  his hands on her waist (doggy) slipped into the gaps between them and pushed only the butt's sides
  in, which left a shelf at its edge.
  Since v0.37 the collision body gives way only where the soft regions are full (`BODY_COL_SOFT`). A
  point keeps its physics-bone share in full above 70% of its region's most, and none below 35%; the
  rest goes to the bone the region hangs from. The butt's and belly's sides, over the hips and ribs,
  barely move when pressed.
- **Two-sided genitals.** The genitals' material draws both sides of each triangle. Seen with the camera inside
  her, the one-sided canal showed only its far half, as if it wrapped half the shaft.
- **The anus ring is centred on the anus.** GT's anus opening is a short slit, and the ring sat 0.17
  forward of its middle, at its front end. The whole slit then hung on the ring's back bones, so when
  the ring opened round him they carried the hole back off him (the SMP wireframe showed
  `SB AnusRim_11` and `_12` on the hole) while the skin in front stretched over him. The ring is now
  centred on GT's own anus point, so the slit splits between its front and back bones.
- **The vagina's entrance rides on its own rings.** The back of the vaginal canal's entrance, 0.5 to
  1.5 units in, rode up to half on the anus's outer ring, and tore away from the canal once the canal
  followed him. The vagina's inner skin now takes none of the anus's rings.
- **Nearby bones move with the opening in use.** The vagina's and anus's centres are 4.1 units apart,
  and about 1,200 points of skin between them ride partly on each one's bones. Up to v0.23 only the
  opening in use moved, so an open anus had the skin on its vagina side hanging across it as a sheet.
  The hood folded the same way: 677 of its points ride on GT's `Clitoral1`, which stayed put. Now every
  bone near the opening in use follows that opening's stretch: the other opening's rings, and GT's lip
  and anus bones. Each moves as the skin at its distance does, easing to nothing at the edge of the
  opening's reach.
- **No shelf round the vulva.** GT sculpts its body round the vulva. Our patch of GT's skin
  stood up to 0.4 units proud of CBBE in a band either side of the slit and up into the mons, then
  dropped back to CBBE at the patch's edge, which showed in game as a shelf (vanilla CBBE has none).
  The patch now lies on CBBE's surface everywhere except close to the slit and anus edges, which join
  the genitals mesh (`FLATTEN`, `FLATTEN_KEEP` in `tools/genitals.py`). The collision pads beside the
  lips then came out through the skin; they're now kept 0.15 under it (`PAD_UNDER`).
- **No texture flecks beside the lips.** CBBE's texture islands are joined by vertex index, and the one
  beside the vulva folds so that two of its edges (v 0.03 and 0.99) meet on her body. A patch
  triangle's corners were taken from that island alone, so up to 13 triangles stretched over the whole
  texture and drew pale flecks with dark strokes. Each corner's UV now comes from CBBE triangles near its
  triangle's own in UV as well (`SAME_CHART`), and a triangle at the hole's edge maps through a CBBE
  triangle of the vertex it joins.
- **The anus's throat opens with its rim.** Up to v0.22 the anal canal took over from GT's anus
  weights over its first unit of depth. That left the throat on GT's anus bones, which are no longer
  driven, so it stayed shut while the skin round it opened. The canal now rides on its rings from the
  mouth in, and the first of those rings is the driven rim.
- **At most 131 bones a shape.** v0.22's CBBE (199 bones) and genitals (162) crashed the game as the
  body was drawn; v0.21's largest, 131, did not. So CBBE and the vulva's seam with it ride on the outer
  rings only, and a mesh over the cap is drawn as several shapes (the genitals as `SoftBodyGenitals`
  and `SoftBodyGenitals2`), each with its own slider data.
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
