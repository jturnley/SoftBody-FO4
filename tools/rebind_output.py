"""Move SoftBody's genital bones onto the skin in a BodySlide-built body.

BodySlide moves vertices by the sliders but never moves bones. A preset that reshapes the crotch (one tested moved it
about 2 units) leaves the openings' skin away from the bones that carry it, so everything placed by bone misses what
is drawn: the plugin's aim and rims, the collision pads, the canals' springs.

This moves each genital node (every node under Pelvis_skin) by the sliders' displacement of the skin around it,
measured against SoftBody's reference mesh, and rebinds the skin to match, so the body looks exactly as built at rest
and the bones sit where the skin is. Run it after every BodySlide build of the SoftBody FO4 body:

  python -I tools/rebind_output.py <built FemaleBody.nif> [reference CBBEBodySoftBody.nif]

The built NIF is rewritten in place; the original is kept as <name>.nif.bak (the first time only). Running it twice
does nothing more: the displacement is measured against the reference each time, and bones already moved are moved
back to their reference place first.
"""
import os
import shutil
import struct
import sys

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dents import SkinnedShape, BONE_STRIDE  # noqa: E402
from nif_smp import Nif  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFERENCE = os.path.join(ROOT, r"mod\Tools\BodySlide\ShapeData\CBBE SoftBody\CBBEBodySoftBody.nif")
SKIN_SHAPES = ("SoftBodyVulva", "SoftBodyGenitals")          # the drawn skin: the displacement field
ALL_SHAPES = SKIN_SHAPES + ("SoftBodyVaginaOut", "SoftBodyAnus", "SoftBodyAnalCanal", "SoftBodyVaginalCanal",
                             "CBBE")   # CBBE: its skin round the openings rides on the rims too
FIELD_K = 24          # skin points averaged for a node's move
FIELD_SIGMA = 0.8     # game units: their weight falls off over this from the nearest


def node_transform(nif, i):
    """(translation offset in the block, translation, rotation row-major 3x3)"""
    _, _, _, r = nif.net(i)
    at = r.pos + 4                    # after the flags
    data = nif.blocks[i][1]
    t = np.array(struct.unpack_from("<3f", data, at))
    R = np.array(struct.unpack_from("<9f", data, at + 12)).reshape(3, 3)
    return at, t, R


def subtree(nif, top):
    """[(node, parent)] under top, parents first"""
    out, stack = [], [top]
    while stack:
        p = stack.pop()
        for c in nif.node_children(p):
            if c >= 0 and nif.is_node(c):
                out.append((c, p))
                stack.append(c)
    return out


def positions(nif, shape):
    s = SkinnedShape(nif, shape)
    return s, np.array([s.position(v) for v in range(s.nvert)], float)


def pelvis_frame(nif):
    """mesh space <-> Pelvis_skin space, from a genital shape's bind (bone = R v + t)"""
    s = SkinnedShape(nif, "SoftBodyGenitals")
    _, rot, t = s.bind(s.names.index("Pelvis_skin"))
    return np.array(rot).reshape(3, 3), np.array(t)


def node_places(nif, top, Rb, tb):
    """each node's position in mesh space and its parent's rotation, in Pelvis_skin's frame"""
    world = {top: (np.zeros(3), np.eye(3))}
    places = {}
    for c, p in subtree(nif, top):
        _, t, R = node_transform(nif, c)
        T, Rp = world[p]
        world[c] = (T + Rp @ t, Rp @ R)
        places[c] = (Rb.T @ (world[c][0] - tb), p)
    return world, places


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    built_path = argv[1]
    ref_path = argv[2] if len(argv) > 2 else REFERENCE
    ref, out = Nif(ref_path), Nif(built_path)
    Rb, tb = pelvis_frame(ref)
    top_ref, top_out = ref.find_node("Pelvis_skin"), out.find_node("Pelvis_skin")

    # the sliders' displacement of the skin, vertex for vertex
    P, D = [], []
    for name in SKIN_SHAPES:
        _, pr = positions(ref, name)
        _, po = positions(out, name)
        if len(pr) != len(po):
            raise SystemExit("%s: %d vertices in the build, %d in the reference: not this body's build?" % (name, len(po), len(pr)))
        P.append(pr)
        D.append(po - pr)
    P, D = np.concatenate(P), np.concatenate(D)
    tree = cKDTree(P)

    def move_at(p):
        d, i = tree.query(p, FIELD_K)
        w = np.exp(-((d - d[0]) / FIELD_SIGMA) ** 2)
        return (w[:, None] * D[i]).sum(0) / w.sum()

    # each node: its reference place, moved by the field there
    _, ref_places = node_places(ref, top_ref, Rb, tb)
    ref_by_name = {ref.net(c)[0]: (m, ref.net(p)[0]) for c, (m, p) in ref_places.items()}
    out_world, out_places = node_places(out, top_out, Rb, tb)
    names = {c: out.net(c)[0] for c in out_places}
    missing = [n for n in names.values() if n not in ref_by_name]
    if missing:
        raise SystemExit("nodes not in the reference: %s" % missing[:5])
    new_mesh = {c: ref_by_name[n][0] + move_at(ref_by_name[n][0]) for c, n in names.items()}
    new_pelvis = {c: Rb @ m + tb for c, m in new_mesh.items()}
    new_pelvis[top_out] = np.zeros(3)

    # the nodes' local translations (rotations kept), parents first
    for c, p in subtree(out, top_out):
        at, _, _ = node_transform(out, c)
        Rp = out_world[p][1]
        local = Rp.T @ (new_pelvis[c] - new_pelvis[p])
        data = bytearray(out.blocks[c][1])
        struct.pack_into("<3f", data, at, *local)
        out.blocks[c][1] = bytes(data)

    # the binds: a bone moved by delta from its reference place takes t' = t_ref - R delta, so the skin stays put
    moved = {names[c]: new_mesh[c] - ref_by_name[names[c]][0] for c in names}
    for shape in ALL_SHAPES:
        so = SkinnedShape(out, shape)
        sr = SkinnedShape(ref, shape)
        for k, bone in enumerate(so.names):
            if bone not in moved:
                continue
            kr = sr.names.index(bone)
            sphere, rot, t = sr.bind(kr)
            R = np.array(rot).reshape(3, 3)
            d = R @ moved[bone]
            at = 4 + BONE_STRIDE * k
            struct.pack_into("<4f", so.binds, at, sphere[0] - d[0], sphere[1] - d[1], sphere[2] - d[2], sphere[3])
            struct.pack_into("<3f", so.binds, at + 52, *(np.array(t) - d))
        out.blocks[so.bone_data][1] = bytes(so.binds)

    big = sorted(((np.linalg.norm(v), n) for n, v in moved.items()), reverse=True)
    print("moved %d genital nodes onto the skin: largest %.2f (%s), median %.2f" %
          (len(moved), big[0][0], big[0][1], float(np.median([b for b, _ in big]))))
    for n in ("SB VagIn_0", "NPC L Pussy02", "VaginaB1", "NPC LT Anus2", "SB AnusT1"):
        if n in moved:
            print("  %-14s by (%.2f, %.2f, %.2f)" % (n, *moved[n]))
    backup = built_path + ".bak"
    if not os.path.exists(backup):
        shutil.copy2(built_path, backup)
    out.write(built_path)
    print("written", built_path, "(original kept as %s)" % os.path.basename(backup))


if __name__ == "__main__":
    main(sys.argv)
