"""Build the SoftBody FO4 prototype mod folder from the user's installed CBBE.

  python build_prototype.py [CBBE mod dir] [output mod dir]

Writes into the output mod dir (default: ../mod):
  meshes/actors/character/characterassets/FemaleHands.nif   CBBE hands + SMP link
  Tools/BodySlide/ShapeData/CBBE SoftBody/CBBEBodySoftBody.nif  CBBE body + SMP link
  Tools/BodySlide/SliderSets/SoftBodyFO4.osp                 set "CBBE Body SoftBody"
  Tools/BodySlide/SliderGroups/SoftBodyFO4.xml               adds the set to group CBBE
  meshes/SoftBodyFO4/*.xml                                   physics XMLs from src/, with the
                                                             generated dent bones filled in
"""
import os
import shutil
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nif_smp import Nif, SMP_KEY  # noqa: E402
from dents import add_dents  # noqa: E402
import genitals  # noqa: E402
import genital_textures  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CBBE = sys.argv[1] if len(sys.argv) > 1 else r"E:\FO4-MO2\mods\Caliente's Beautiful Bodies Enhancer -CBBE-"
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "mod")
# GT Softbody (Skyrim SE) for its genitals: conversion allowed with credit to Goutou
GT = sys.argv[3] if len(sys.argv) > 3 else r"D:\SkyrimSE-MO2\mods\GT - Softbody v3.37"
GT_BODY = r"CalienteTools\BodySlide\ShapeData\GT_Softbody (CBBE)\GT_Softbody (CBBE).nif"
GT_COLLISION = r"CalienteTools\BodySlide\ShapeData\GT_Softbody_HVACollision (CBBE)\GT_Softbody_HVACollision (CBBE).nif"
# GT's in-game collision NIF: it carries the genital node tree its springs hang from
GT_SKELETON = r"meshes\actors\character\character assets\Xml Addon\SMP Object FemaleHVA_0.nif"
# The openings' collision shapes: (tag, margin, penetration). Per-vertex (each point a sphere of margin x its bone's
# margin-multiplier: 2 on the canal bones, 1 elsewhere), and colliding with everything but each other, as a body
# whose lip and anus proxies do collide in FO4 has them. GT's per-triangle pads (margin 0.1-0.2) built in SMP but
# never collided with A-Body's penis, itself per-triangle in 4Stim scenes (MaleBodyHard.xml).
COLLISION_SHAPES = {"SoftBodyVaginaOut": ("SB_Vagina", 0.35, 0.8), "SoftBodyVaginaDeep": ("SB_Vagina", 0.15, 0.8),
                    "SoftBodyAnus": ("SB_Anus", 0.3, 0.8), "SoftBodyAnalCanal": ("SB_Anus", 0.5, 0.8)}
# ... and not with A-Body's balls ("Balls"): in doggy they hit the pads and were thrown up; A-Body's own anus shapes
# skip them too
NO_COLLIDE_WITH = ("SB_Vagina", "SB_Anus", "Balls")
# The game's own Data (for the skin material the genitals' material is made from)
GAME_DATA = sys.argv[4] if len(sys.argv) > 4 else r"E:\FO4-MO2\Fallout 4\Data"

BODY_XML = r"meshes\SoftBodyFO4\SoftBody-CBBE.xml"
HANDS_XML = r"meshes\SoftBodyFO4\SoftBody-Hands.xml"
SET_NAME = "CBBE Body SoftBody"
DATA_FOLDER = "CBBE SoftBody"
BODY_NIF = "CBBEBodySoftBody.nif"
# Skin bones that get a simulated "<bone>_SB" child (FO4 SMP won't move skeleton bones)
PHYSICS_BONES = ("LBreast_skin", "RBreast_skin", "LButtFat_skin", "RButtFat_skin", "Belly_skin")
PHYSICS_SUFFIX = "_SB"
# Hidden second geometry: FO4 renames a single-shape armor's geometry to
# "<ARMA>[sex]/<ARMO>" on attach, which would hide the shape name from SMP
NAME_ANCHOR = "SoftBodyFO4NameAnchor"
GENITAL_OSD = "SoftBodyGenitals.osd"   # slider data for the transplanted genital shapes
SKIN_D = r"Textures\Actors\Character\BaseHumanFemale\FemaleBody_d.dds"   # the skin the genitals are matched to
# Dent bones per soft region: (count, {donor skin bone: share moved onto the dents})
DENTS = {
    "LBreast_skin": (12, {"Chest_skin": 0.5}),
    "RBreast_skin": (12, {"Chest_skin": 0.5}),
    "LButtFat_skin": (8, {}),
    "RButtFat_skin": (8, {}),
    "Belly_skin": (9, {}),
}


def patch(src, dst, xml_path, shape, physics_bones=(), dents=None, genitals_from=None):
    """Returns ({region bone: [dent bone names]}, the transplanted genital meshes or None)."""
    nif = Nif(src)
    meshes = None
    names = [nif.net(i)[0] for i in range(len(nif.blocks)) if nif.blocks[i][0].endswith("TriShape")]
    if shape not in names:
        raise SystemExit("%s: shape %r not found (have %s)" % (src, shape, names))
    for bone in physics_bones:
        nif.split_physics_bone(bone, PHYSICS_SUFFIX)
    made = {}
    for bone, (count, donors) in (dents or {}).items():
        region = bone + PHYSICS_SUFFIX
        made[bone] = add_dents(nif, shape, region, count, region, donors)
        print("  %s: %d dent bones" % (region, len(made[bone])))
    # after the split and the dents: the genitals' seam copies CBBE's FINAL weights, or the two sides of the seam
    # follow different bones and it tears open under physics
    if genitals_from:
        cbbe, meshes, bones, report = genitals.build(nif, genitals_from, os.path.join(GT, GT_COLLISION),
                                                     os.path.join(GT, GT_SKELETON))
        tex = ["Textures\\" + t.replace("/", "\\") for t in genital_textures.TEXTURES]
        genitals.add_to_nif(nif, cbbe, meshes, bones,
                            materials={genitals.GENITALS: (genital_textures.MATERIAL, tex)})
        made_tex = genital_textures.write(OUT, meshes[genitals.GENITALS], report["vagina_vertices"],
                                          report["fit_R"], GT, GAME_DATA, meshes[genitals.VULVA],
                                          os.path.join(CBBE, SKIN_D))
        print("  genital textures: skin gain %s over %d rim vertices; normal map covers %.1f%%; material %s" % (
            made_tex["skin_gain"].round(3), made_tex["rim_vertices"], 100 * made_tex["normal_coverage"],
            made_tex["material"]))
        srgb = lambda c: np.round(np.clip(c, 0, 1) ** (1 / 2.2) * 255).astype(int).tolist()
        print("  clitoral hood: %d texels faded to the skin; hood tip now %s, skin at the rim %s (sRGB)" % (
            made_tex["hood_texels"], srgb(made_tex["hood_colour"]), srgb(made_tex["skin_colour"])))
        print("  genitals: fit scale %.4f, %.1f deg, residual median %.2f max %.2f; cut %d CBBE triangles; "
              "%s %d vertices, %s %d vertices; %d genital bones" % (
                  report["fit_scale"], report["fit_rotation_deg"], report["fit_median"], report["fit_max"],
                  report["cut_triangles"], genitals.VULVA, report["vulva_vertices_after_uv_split"],
                  genitals.GENITALS, report["genital_vertices"], len(bones)))
        print("  collision: %s" % ", ".join("%s %d triangles" % kv for kv in report["collision_triangles"].items()))
        print("  spring anchors: %d, GT's node tree off its skin binds by at most %.4f" % (
            len(genitals.GT_ANCHORS) + 1, report["anchor_mismatch"]))
        meshes = {name: dict(src=m["src"], hidden=m.get("hidden", False),
                             body_bones=sorted({b for w in m["weights"] for b in w if not genitals.is_genital(b)}))
                  for name, m in meshes.items()}
        meshes["bones"] = sorted(bones)
    nif.add_hidden_twin(shape, NAME_ANCHOR)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    idx = nif.attach_smp(xml_path)
    nif.write(dst)
    key, value = Nif(dst).string_extra(idx)
    assert (key, value) == (SMP_KEY, xml_path), (key, value)
    print("  %s -> %s" % (os.path.relpath(dst, OUT), value))
    return made, meshes


def collision_xml(collision):
    """per-vertex-shapes for the openings. Any body bones a pad is weighted to get a weight threshold of 1, so only
    the genital bones' share collides (the pads are built wholly on genital bones, so there are normally none)."""
    lines = []
    for name, body_bones in collision.items():
        tag, margin, penetration = COLLISION_SHAPES[name]
        lines += ['  <per-vertex-shape name="%s">' % name,
                  '    <margin>%g</margin>' % margin,
                  '    <penetration>%g</penetration>' % penetration,
                  '    <shared>public</shared>',
                  '    <tag>%s</tag>' % tag]
        lines += ['    <no-collide-with-tag>%s</no-collide-with-tag>' % t for t in NO_COLLIDE_WITH]
        lines += ['    <weight-threshold bone="%s">1</weight-threshold>' % b for b in body_bones]
        lines += ['  </per-vertex-shape>']
    return lines


def write_xml(rel, dents=None, collision=None):
    """Copy a physics XML from src/ to the mod, filling in the dent and collision markers.
    collision: {shape: [its body (non-genital) bones]}"""
    text = open(os.path.join(ROOT, "src", os.path.basename(rel)), encoding="utf-8").read()
    if dents:
        bones = []
        constraints = []
        for region, names in dents.items():
            for name in names:
                bones.append('  <bone name="%s" template="dent"/>' % name)
                constraints.append('  <generic-constraint bodyA="%s" bodyB="%s%s" template="dentSpring"/>' % (name, region, PHYSICS_SUFFIX))
        markers = [("<!-- @DENT_BONES@ -->", bones), ("<!-- @DENT_CONSTRAINTS@ -->", constraints),
                   ("<!-- @COLLISION_SHAPES@ -->", collision_xml(collision or {}))]
        for marker, lines in markers:
            assert marker in text, (rel, marker)
            text = text.replace("  " + marker, "\n".join(lines))
    dst = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, "w", encoding="utf-8").write(text)


def check_xml(rel, shape, nif_bones=()):
    """nif_bones: bones added to the NIF (the genitals' own and their anchors) that the XML must declare"""
    path = os.path.join(OUT, rel)
    root = ET.parse(path).getroot()
    declared = {e.get("name") for e in root if e.tag == "bone"}
    missing = set(nif_bones) - declared
    assert not missing, "%s: NIF bones not declared: %s" % (rel, sorted(missing))
    shapes = [e.get("name") for e in root if e.tag in ("per-triangle-shape", "per-vertex-shape")]
    assert shape in shapes, (rel, shapes)
    templates = {e.get("name") for e in root if e.tag.endswith("-default") and e.get("name")}
    bones = {e.get("name") for e in root if e.tag == "bone"}
    for e in root:
        t = e.get("template")
        assert t is None or t in templates, "%s: unknown template %s" % (rel, t)
        if e.tag == "generic-constraint":
            for side in ("bodyA", "bodyB"):
                assert e.get(side) in bones, "%s: %s %s is not a declared bone" % (rel, side, e.get(side))
    print("  %s ok (%d bones, %d shapes)" % (rel, len(bones), len(shapes)))


def write_slider_set(extra=None):
    """extra: {shape: {CBBE data name: (new data name, offsets)}} for shapes added to the body, whose slider
    data goes into GENITAL_OSD in the set's own data folder"""
    osp = os.path.join(CBBE, r"Tools\BodySlide\SliderSets\CBBE.osp")
    tree = ET.parse(osp)
    src = next(s for s in tree.getroot() if s.get("name") == "CBBE Body")
    src.set("name", SET_NAME)
    src.find("DataFolder").text = DATA_FOLDER
    src.find("SourceFile").text = BODY_NIF
    for shape in src.findall("Shape"):
        shape.set("DataFolder", "CBBE")  # morph data (.osd) stays in CBBE's own folder
    if extra:
        records = {}
        last_shape = src.findall("Shape")[-1]
        at = list(src).index(last_shape) + 1
        for k, name in enumerate(extra):
            el = ET.Element("Shape", target=name, DataFolder=DATA_FOLDER)
            el.text = name
            src.insert(at + k, el)
        for slider in src.findall("Slider"):
            for data in list(slider.findall("Data")):
                for name, recs in extra.items():
                    if data.get("name") in recs:
                        new, offsets = recs[data.get("name")]
                        records[new] = offsets
                        el = ET.SubElement(slider, "Data", name=new, target=name, local="true")
                        el.text = "%s\\%s" % (GENITAL_OSD, new)
        genitals.write_osd(os.path.join(OUT, r"Tools\BodySlide\ShapeData", DATA_FOLDER, GENITAL_OSD), records)
        print("  %s: %d records for %s" % (GENITAL_OSD, len(records), ", ".join(extra)))
    out = ET.ElementTree(ET.Element("SliderSetInfo", version="1"))
    out.getroot().append(src)
    ET.indent(out, space="    ")
    dst = os.path.join(OUT, r"Tools\BodySlide\SliderSets\SoftBodyFO4.osp")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    out.write(dst, encoding="utf-8", xml_declaration=True)
    print("  %s (%d sliders)" % (os.path.relpath(dst, OUT), len(src.findall("Slider"))))

    groups = ET.Element("SliderGroups")
    ET.SubElement(ET.SubElement(groups, "Group", name="CBBE"), "Member", name=SET_NAME)
    gtree = ET.ElementTree(groups)
    ET.indent(gtree, space="    ")
    gdst = os.path.join(OUT, r"Tools\BodySlide\SliderGroups\SoftBodyFO4.xml")
    os.makedirs(os.path.dirname(gdst), exist_ok=True)
    gtree.write(gdst, encoding="utf-8", xml_declaration=True)
    print("  %s" % os.path.relpath(gdst, OUT))


def main():
    print("NIFs")
    dents, genital_meshes = patch(os.path.join(CBBE, r"Tools\BodySlide\ShapeData\CBBE\CBBEBody.nif"),
                  os.path.join(OUT, r"Tools\BodySlide\ShapeData", DATA_FOLDER, BODY_NIF), BODY_XML, "CBBE", PHYSICS_BONES, DENTS,
                  os.path.join(GT, GT_BODY))
    # Fallback copy of the morph data in case BodySlide ignores the per-shape DataFolder
    shutil.copyfile(os.path.join(CBBE, r"Tools\BodySlide\ShapeData\CBBE\CBBEBody.osd"),
                    os.path.join(OUT, r"Tools\BodySlide\ShapeData", DATA_FOLDER, "CBBEBody.osd"))
    patch(os.path.join(CBBE, r"meshes\actors\character\characterassets\FemaleHands.nif"),
          os.path.join(OUT, r"meshes\actors\character\characterassets\FemaleHands.nif"), HANDS_XML, "BaseFemaleHands3rd:0")
    print("physics XML")
    genital_bones = genital_meshes.pop("bones") if genital_meshes else ()
    collision = {name: m["body_bones"] for name, m in (genital_meshes or {}).items() if m["hidden"]}
    write_xml(BODY_XML, dents, collision)
    write_xml(HANDS_XML)
    check_xml(BODY_XML, "CBBE", genital_bones)
    check_xml(HANDS_XML, "BaseFemaleHands3rd:0")
    print("BodySlide")
    write_slider_set(genitals.slider_data(genital_meshes, os.path.join(CBBE, r"Tools\BodySlide\ShapeData\CBBE\CBBEBody.osd"))
                     if genital_meshes else None)


if __name__ == "__main__":
    main()
