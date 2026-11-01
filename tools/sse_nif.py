"""Read skinned BSTriShapes from Skyrim SE NIFs (20.2.0.7, BS version 100).

Only what the GT Softbody port needs: per shape its vertex positions, UVs,
triangles, per-vertex bone weights (by bone name) and each bone's bind
transform. SSE keeps a skinned shape's vertex data in its NiSkinPartition and
the weights in NiSkinData; the BSTriShape itself carries none.

Usage:
  python sse_nif.py <file.nif>          list shapes
"""
import struct
import sys


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def take(self, fmt):
        v = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += struct.calcsize("<" + fmt)
        return v if len(v) > 1 else v[0]

    def string(self):
        n = self.take("I")
        s = self.data[self.pos:self.pos + n].decode("latin-1")
        self.pos += n
        return s


class SseNif:
    def __init__(self, path):
        data = open(path, "rb").read()
        r = Reader(data, data.index(b"\n") + 1)
        version, _, _, nblocks, self.bs_version = r.take("IBIII")
        if version != 0x14020007 or self.bs_version != 100:
            raise ValueError("not a Skyrim SE NIF (version %08x, bsver %d)" % (version, self.bs_version))
        for _ in range(3):  # author, process script, export script
            r.pos += 1 + data[r.pos]
        types = [r.string() for _ in range(r.take("H"))]
        type_ix = [r.take("H") for _ in range(nblocks)]
        sizes = [r.take("I") for _ in range(nblocks)]
        nstrings = r.take("I")
        r.take("I")
        self.strings = [r.string() for _ in range(nstrings)]
        ngroups = r.take("I")
        r.pos += 4 * ngroups
        self.blocks = []
        for t, size in zip(type_ix, sizes):
            self.blocks.append((types[t & 0x7FFF], data[r.pos:r.pos + size]))
            r.pos += size

    def name(self, i):
        idx = struct.unpack_from("<I", self.blocks[i][1], 0)[0]
        return None if idx == 0xFFFFFFFF else self.strings[idx]

    def shapes(self):
        return [self.name(i) for i, (t, _) in enumerate(self.blocks) if t == "BSTriShape"]

    def shape(self, name):
        i = next(k for k, (t, _) in enumerate(self.blocks) if t == "BSTriShape" and self.name(k) == name)
        return SseShape(self, i)

    def node_transforms(self):
        """{node name: (world rotation W, world position)}, composed down the NiNode tree; W maps the node's
        local axes to the world (v_world = W v_local), so a skin bind's rotation is W transposed"""
        local, parent = {}, {}
        for i, (t, d) in enumerate(self.blocks):
            if t not in ("NiNode", "BSFadeNode", "BSLeafAnimNode", "BSMultiBoundNode", "BSValueNode"):
                continue
            r = Reader(d, 4)
            nextra = r.take("I")
            r.pos += 4 * nextra + 4 + 4              # extra data, controller, flags
            tr, rot, scale = r.take("3f"), r.take("9f"), r.take("f")
            r.take("i")                              # collision
            local[i] = (self.name(i), [rot[0:3], rot[3:6], rot[6:9]], tr, scale)
            for k in [r.take("i") for _ in range(r.take("I"))]:
                if k >= 0:
                    parent[k] = i
        world = {}
        def compose(i):
            if i not in world:
                _, R, t, s = local[i]
                if parent.get(i) in local:
                    PR, Pt, Ps = compose(parent[i])
                    R = [[sum(PR[a][k] * R[k][b] for k in range(3)) for b in range(3)] for a in range(3)]
                    t = tuple(Pt[a] + Ps * sum(PR[a][k] * t[k] for k in range(3)) for a in range(3))
                    s = Ps * s
                world[i] = (R, t, s)
            return world[i]
        return {local[i][0]: compose(i)[:2] for i in local}


class SseShape:
    """positions [(x,y,z)], uvs [(u,v)], triangles [(a,b,c)], weights [{bone: w}], binds {bone: (rot9, t3, scale)}"""

    def __init__(self, nif, i):
        self.name = nif.name(i)
        r = Reader(nif.blocks[i][1])
        r.take("I")
        nextra = r.take("I")
        r.pos += 4 * nextra           # extra data
        r.take("i")                   # controller
        self.flags = r.take("I")
        r.take("13f")                 # transform (identity on GT's shapes)
        r.take("i")                   # collision
        r.take("4f")                  # bounding sphere
        skin, self.shader, self.alpha = r.take("iii")
        self.desc = r.take("Q")
        ntri, nvert = r.take("HH")

        # skin instance: data, partition, skeleton root, bones
        sr = Reader(nif.blocks[skin][1])
        skin_data, partition, _ = sr.take("iii")
        bones = [sr.take("i") for _ in range(sr.take("I"))]
        self.bone_names = [nif.name(b) for b in bones]

        # vertex data and triangles from the partition
        pr = Reader(nif.blocks[partition][1])
        nparts, data_size, vsize = pr.take("III")
        vdesc = pr.take("Q")
        vstart = pr.pos
        nverts = data_size // vsize if vsize else 0
        uv_at = ((vdesc >> 8) & 0xF) * 4
        self.positions = [struct.unpack_from("<3f", pr.data, vstart + v * vsize) for v in range(nverts)]
        self.uvs = [struct.unpack_from("<2e", pr.data, vstart + v * vsize + uv_at) for v in range(nverts)]
        pr.pos = vstart + data_size
        self.triangles = []
        for _ in range(nparts):
            nv, nt, nb, ns, nw = pr.take("HHHHH")
            pr.pos += 2 * nb
            if pr.take("B"):
                pr.pos += 2 * nv                      # vertex map
            if pr.take("B"):
                pr.pos += 4 * nv * nw                 # vertex weights
            strips = [pr.take("H") for _ in range(ns)]
            if pr.take("B"):
                pr.pos += 2 * sum(strips) if ns else 6 * nt
            if pr.take("B"):
                pr.pos += nv * nw                     # bone indices
            pr.take("H")                              # LOD level
            pr.take("Q")                              # vertex desc
            self.triangles += [pr.take("3H") for _ in range(nt)]   # global vertex indices

        # weights and bind transforms from the skin data
        dr = Reader(nif.blocks[skin_data][1])
        dr.take("13f")                                # skin transform
        nb = dr.take("I")
        has_weights = dr.take("B")
        self.weights = [dict() for _ in range(nverts)]
        self.binds = {}
        for b in range(nb):
            rot, t, scale = dr.take("9f"), dr.take("3f"), dr.take("f")
            dr.take("4f")                             # bounding sphere
            self.binds[self.bone_names[b]] = (rot, t, scale)
            if has_weights:
                for _ in range(dr.take("H")):
                    v, w = dr.take("Hf")
                    if w > 0:
                        self.weights[v][self.bone_names[b]] = w

    def bone_position(self, bone):
        """The bone's origin in skin (mesh) space: the bind maps mesh to bone space as R*v + t."""
        rot, t, scale = self.binds[bone]
        # v = R^T (o - t) / s with o = 0
        return tuple(-sum(rot[3 * r + c] * t[r] for r in range(3)) / scale for c in range(3))


def main(argv):
    nif = SseNif(argv[1])
    for name in nif.shapes():
        s = nif.shape(name)
        lo = [round(min(p[a] for p in s.positions), 1) for a in range(3)]
        hi = [round(max(p[a] for p in s.positions), 1) for a in range(3)]
        print("%-22s verts %6d tris %6d bones %3d  bounds %s .. %s" % (
            name, len(s.positions), len(s.triangles), len(s.bone_names), lo, hi))


if __name__ == "__main__":
    main(sys.argv)
