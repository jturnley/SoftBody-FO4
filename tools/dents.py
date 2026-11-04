"""Local dent bones for a skinned FO4 shape.

A soft region (for example LBreast_skin_SB) moves as one piece. To let a hand
press a dent into part of it, the region's vertices are clustered and each
cluster gets a small physics bone, a child of the region bone. The region's
skin weight is spread over the nearest few dent bones, so a contact on one
cluster only moves the skin around it.

Positions are worked in the region bone's bind space (skin data maps mesh
space to bone space as R*v + t), so a dent bone is a plain translated child of
the region bone and skins identically to it at rest.
"""
import math
import struct

BONE_STRIDE = 68           # BSSkin::BoneData entry: sphere 4f, rotation 9f, translation 3f, scale f
NEIGHBOURS = 3             # dent bones sharing each vertex
MIN_WEIGHT = 0.02          # region weight below this stays on the region bone's neighbours as-is


class SkinnedShape:
    """Vertex positions and skin weights of one BSTriShape / BSSubIndexTriShape."""

    def __init__(self, nif, shape):
        self.nif = nif
        self.index = next(i for i, (t, _) in enumerate(nif.blocks) if t.endswith("TriShape") and nif.net(i)[0] == shape)
        data = nif.blocks[self.index][1]
        # the fixed offsets below are for a shape with no extra data; BodySlide gives its output's body one (the SMP
        # file's), which moves everything after it by 4
        o = self.o = 4 * struct.unpack_from("<I", data, 4)[0]
        self.skin = struct.unpack_from("<i", data, o + 88)[0]
        desc = struct.unpack_from("<Q", data, o + 100)[0]
        if not (desc >> 44) & 0x40:
            raise ValueError("%s has no skin weights in its vertices" % shape)
        if (desc >> 44) & 0x400:
            raise ValueError("%s uses full-precision vertices (not handled)" % shape)
        self.nvert = struct.unpack_from("<H", data, o + 112)[0]
        self.stride = (desc & 0xF) * 4
        self.skin_at = ((desc >> 28) & 0xF) * 4
        self.vert_at = o + 118
        self.data = bytearray(data)
        inst = nif.blocks[self.skin][1]
        _, self.bone_data, nb = struct.unpack_from("<iiI", inst, 0)
        self.bones = list(struct.unpack_from("<%di" % nb, inst, 12))
        self.names = [nif.net(b)[0] for b in self.bones]
        self.binds = bytearray(nif.blocks[self.bone_data][1])

    def position(self, v):
        return struct.unpack_from("<3e", self.data, self.vert_at + v * self.stride)

    def influences(self, v):
        at = self.vert_at + v * self.stride + self.skin_at
        weights = struct.unpack_from("<4e", self.data, at)
        indices = struct.unpack_from("<4B", self.data, at + 8)
        return [(i, w) for i, w in zip(indices, weights) if w > 0]

    def set_influences(self, v, influences):
        influences = sorted(influences, key=lambda iw: -iw[1])[:4]
        total = sum(w for _, w in influences)
        influences = [(i, w / total) for i, w in influences]
        weights = [struct.unpack("<e", struct.pack("<e", w))[0] for _, w in influences]
        weights[0] += 1 - sum(weights)  # keep the half-float sum at 1
        influences = [(i, w) for (i, _), w in zip(influences, weights)] + [(0, 0.0)] * (4 - len(influences))
        at = self.vert_at + v * self.stride + self.skin_at
        struct.pack_into("<4e", self.data, at, *[w for _, w in influences])
        struct.pack_into("<4B", self.data, at + 8, *[i for i, _ in influences])

    def bind(self, k):
        at = 4 + BONE_STRIDE * k
        return (struct.unpack_from("<4f", self.binds, at), struct.unpack_from("<9f", self.binds, at + 16),
                struct.unpack_from("<3f", self.binds, at + 52))

    def to_bone(self, k, p):
        _, rot, t = self.bind(k)
        return tuple(sum(rot[3 * r + c] * p[c] for c in range(3)) + t[r] for r in range(3))

    def add_bone(self, node, sphere, rot, t):
        self.bones.append(node)
        self.binds += struct.pack("<4f9f3ff", *sphere, *rot, *t, 1.0)
        return len(self.bones) - 1

    def commit(self):
        nif = self.nif
        nif.blocks[self.index][1] = bytes(self.data)
        struct.pack_into("<I", self.binds, 0, len(self.bones))
        nif.blocks[self.bone_data][1] = bytes(self.binds)
        inst = nif.blocks[self.skin][1]
        root, bone_data, nb = struct.unpack_from("<iiI", inst, 0)
        tail = inst[12 + 4 * nb:]
        nif.blocks[self.skin][1] = struct.pack("<iiI", root, bone_data, len(self.bones)) + struct.pack("<%di" % len(self.bones), *self.bones) + tail


def kmeans(points, weights, k, iterations=20):
    """Weighted k-means, seeded by farthest-point sampling (deterministic)."""
    centres = [max(zip(weights, points))[1]]
    while len(centres) < k:
        centres.append(max(points, key=lambda p: min(math.dist(p, c) for c in centres)))
    for _ in range(iterations):
        sums = [[0.0, 0.0, 0.0, 0.0] for _ in centres]
        for p, w in zip(points, weights):
            j = min(range(k), key=lambda j: math.dist(p, centres[j]))
            s = sums[j]
            s[0] += p[0] * w; s[1] += p[1] * w; s[2] += p[2] * w; s[3] += w
        centres = [tuple(s[a] / s[3] for a in range(3)) if s[3] else c for s, c in zip(sums, centres)]
    return centres


def add_dents(nif, shape, region, count, prefix, donors=None):
    """Split `region` (a skin bone of `shape`) into `count` dent bones.

    donors: {skin bone: fraction} - share of that bone's weight moved onto the
    dent bones, scaled by how strongly the vertex belongs to the region. CBBE
    breasts are at most ~46% LBreast_skin and the rest Chest_skin, so without
    this a dent could only move the skin half as far as the bone.
    Returns the new bone names.
    """
    donors = donors or {}
    skin = SkinnedShape(nif, shape)
    k = skin.names.index(region)
    donor_ix = {skin.names.index(n): f for n, f in donors.items()}
    members = []
    for v in range(skin.nvert):
        w = dict(skin.influences(v)).get(k, 0)
        if w >= MIN_WEIGHT:
            members.append((v, w, skin.to_bone(k, skin.position(v))))
    peak = max(w for _, w, _ in members)
    centres = kmeans([q for _, _, q in members], [w for _, w, _ in members], count)
    spacing = sorted(min(math.dist(c, o) for o in centres if o is not c) for c in centres)[len(centres) // 2]
    sigma = 0.6 * spacing

    sphere, rot, t = skin.bind(k)
    parent = skin.bones[k]
    flags = nif.net(parent)[3].u32()
    names, dent_ix = [], []
    for j, c in enumerate(centres):
        name = "%s%02d" % (prefix, j + 1)
        node = len(nif.blocks)
        nif.blocks.append(["NiNode", struct.pack("<IIiI", nif.add_string(name), 0, -1, flags)
                           + struct.pack("<3f9ff", *c, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1) + struct.pack("<iI", -1, 0)])
        nif.add_child(parent, node)
        dent_sphere = (sphere[0] - c[0], sphere[1] - c[1], sphere[2] - c[2], sphere[3])
        dent_ix.append(skin.add_bone(node, dent_sphere, rot, (t[0] - c[0], t[1] - c[1], t[2] - c[2])))
        names.append(name)

    for v, w, q in members:
        influences = dict(skin.influences(v))
        moved = w
        for d, f in donor_ix.items():
            take = influences.get(d, 0) * f * (w / peak)
            if take:
                influences[d] -= take
                moved += take
        del influences[k]
        nearest = sorted(range(len(centres)), key=lambda j: math.dist(q, centres[j]))[:NEIGHBOURS]
        g = [math.exp(-(math.dist(q, centres[j]) / sigma) ** 2) for j in nearest]
        if sum(g) < 1e-6:
            g = [1.0] + [0.0] * (len(nearest) - 1)
        for j, gj in zip(nearest, g):
            influences[dent_ix[j]] = influences.get(dent_ix[j], 0) + moved * gj / sum(g)
        skin.set_influences(v, [(i, x) for i, x in influences.items() if x > 1e-4])
    skin.commit()
    return names
