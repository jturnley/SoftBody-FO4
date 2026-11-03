"""Transplant GT Softbody's genitals (Skyrim SE, by Goutou) into the FO4 CBBE body.

GT keeps its genitals apart from the body: "2Vagina" (vulva and canal) and
"2Anus" join its body mesh at two open loops, and the outer vulva is sculpted
into the body around them. FO4's CBBE crotch is one closed skin. So:

  1. fit     GT's crotch onto CBBE's (ICP with scale on the skin around it;
             the two bodies share proportions there, so it is near rigid)
  2. cut     a hole in CBBE around the vulva and anus (triangles only: CBBE
             keeps every vertex, so its BodySlide slider data stays valid)
  3. patch   GT's body around the slit and anus, bridged to the hole's edge:
             shape "SoftBodyVulva", skin material, UVs and body weights taken
             from the CBBE surface under it
  4. canals  "2Vagina" + "2Anus": shape "SoftBodyGenitals", GT's own UVs (its
             vagina texture)
  5. bones   GT's genital bones, placed and turned by the fit, as plain
             children of Pelvis_skin in the NIF (FO4 SMP simulates NIF-only
             bones), with the skeleton bones its springs hang from (anchors)
  6. pads    GT's collision pads for the openings, as hidden shapes

GT positions are in Skyrim skeleton space; CBBE's are in its skin space.
"""
import math
import re
import struct

import numpy as np
from scipy.spatial import cKDTree

from dents import SkinnedShape
from sse_nif import SseNif

VULVA, GENITALS = "SoftBodyVulva", "SoftBodyGenitals"
# GT's anus and the front of its slit (the open loops "2Anus" and "2Vagina" join the body at), GT space
ANUS_GT, SLIT_FRONT_GT = (0.0, -5.69, 67.6), (0.0, 2.91, 65.6)
SLIT_GT = (0.0, 0.0, 65.58)
R_PATCH = 1.9       # GT body within this of the anus-slit line is transplanted
R_HOLE = 2.8        # CBBE triangles wholly within this of it are cut
FIT_RING = (3.5, 10.0)   # the skin used for the fit: around the vulva, not on it
WEIGHT_FADE = 1.5        # the vulva's weights go from CBBE's (outer seam) to fixed bones over this distance
OVER_DEPTH = 1.5        # GT triangles further than this from the cut CBBE surface are not "over" it
EASE = 0.8              # the patch's outer band this wide is eased onto CBBE's surface
SAME_UV = 0.05          # corner UVs closer than this are one vertex (CBBE's real seams jump much further)
OPENING_PULL = 0.8      # at the slit and anus edges the skin's genital-bone share goes this much of the way to 1 ...
OPENING_FADE = 1.5      # ... fading out over this distance from them
ANUS_PULL = 0.85        # the anal canal's share on the anus bones goes this much of the way to 1
# The anal canal. GT's is a pocket 2.5 deep and 1 across, moved only by the anus ring; a penis goes 7 deep. Past
# ANAL_KEEP (its depth from the anus ring, which stays as GT has it) it is stretched into a tube ANAL_DEPTH deep and
# ANAL_RADIUS round, closing at the end as GT's does, with rings of four bones of our own at ANAL_RINGS (T, L, B, R
# round it, each opening straight outward) and a collision pad tube at each ring (ANAL_PADS).
ANAL_KEEP = 0.5
ANAL_DEPTH = 8.0
ANAL_RADIUS = 0.4
ANAL_RINGS = (2.0, 4.0, 6.0)
ANAL_SIDES = (("T", 0.0), ("L", 90.0), ("B", 180.0), ("R", -90.0))    # degrees round the canal, from the vagina's side
ANAL_PAD_HALF = 0.9          # each ring's pad tube runs this far either side of it
ANAL_PADS = "SoftBodyAnalCanal"
ANAL_BLEND = 1.0             # from the anus ring (depth 0) the canal's weights go from GT's to the rings' over this
# GT's genital bones (ported) and the anal canal's own; every other bone's share of a vertex is taken from CBBE's
# surface instead
GENITAL_PREFIXES = ("NPC L Pussy", "NPC R Pussy", "Vagina", "Clitoral", "NPC LT Anus", "NPC RT Anus",
                    "NPC LB Anus", "NPC RB Anus", "Pussy", "SB Anus")
# GT's collision for the openings (its HVA collision NIF): small closed pads, one per genital bone, round the opening,
# the canal and the anus; a penis is held in the channel between them. Hidden shapes here, collided per vertex.
COLLISION = {"body_VaginaOut": "SoftBodyVaginaOut", "body_VaginaDeep": "SoftBodyVaginaDeep", "body_anus": "SoftBodyAnus"}
# The Skyrim skeleton bones GT's genital springs hang from. They become kinematic NIF-only nodes here, in GT's own
# orientation (a spring's limits run along its anchor's axes); GT's pelvis comes as GT_PELVIS, as FO4's Pelvis_skin
# is turned differently
GT_ANCHORS = ("Clitoral", "VaginaB", "NPC L Pussy01", "NPC R Pussy01", "VaginaDeep",
              "VaginaDB00", "VaginaDF00", "VaginaDR00", "VaginaDL00",
              "NPC LT Anus1", "NPC RT Anus1", "NPC LB Anus1", "NPC RB Anus1")
GT_PELVIS = ("NPC Pelvis [Pelv]", "GT Pelvis")


# ---------------------------------------------------------------- geometry helpers

def seg_dist(P, a, b):
    a, b = np.asarray(a), np.asarray(b)
    t = np.clip(((P - a) @ (b - a)) / ((b - a) @ (b - a)), 0, 1)
    return np.linalg.norm(P - (a + t[:, None] * (b - a)), axis=1)


def weld(P, digits=4):
    key, ids = {}, np.empty(len(P), int)
    for i, p in enumerate(P):
        ids[i] = key.setdefault(tuple(np.round(p, digits)), len(key))
    return ids


def boundary_loops(tris, ids):
    """Closed loops of open edges, as lists of welded ids, each in winding order."""
    count = {}
    directed = {}
    for a, b, c in tris:
        for x, y in ((a, b), (b, c), (c, a)):
            x, y = ids[x], ids[y]
            k = (min(x, y), max(x, y))
            count[k] = count.get(k, 0) + 1
            directed[k] = (x, y)
    nxt = {}
    for k, n in count.items():
        if n == 1:
            x, y = directed[k]
            nxt[x] = y
    loops, seen = [], set()
    for start in nxt:
        if start in seen:
            continue
        loop, cur = [], start
        while cur not in seen and cur in nxt:
            seen.add(cur)
            loop.append(cur)
            cur = nxt[cur]
        loops.append(loop)
    return loops


def pinches(tris, ids):
    """Welded vertices where a selection's boundary touches itself (more than two open edges meet), so its
    loops cannot be traced as simple curves."""
    count = {}
    for a, b, c in tris:
        for x, y in ((a, b), (b, c), (c, a)):
            k = (min(ids[x], ids[y]), max(ids[x], ids[y]))
            count[k] = count.get(k, 0) + 1
    degree = {}
    for (x, y), n in count.items():
        if n == 1:
            degree[x] = degree.get(x, 0) + 1
            degree[y] = degree.get(y, 0) + 1
    return {v for v, d in degree.items() if d > 2}


def umeyama(a, b):
    ma, mb = a.mean(0), b.mean(0)
    A, B = a - ma, b - mb
    U, S, Vt = np.linalg.svd(B.T @ A / len(a))
    D = np.eye(3)
    if np.linalg.det(U @ Vt) < 0:
        D[2, 2] = -1
    R = U @ D @ Vt
    s = np.trace(np.diag(S) @ D) / (A ** 2).sum(1).mean()
    return s, R, mb - s * R @ ma


def closest_on_triangles(points, P, tris, tree, centroids):
    """For each point: (triangle index, barycentric weights) of the nearest CBBE triangle (among close candidates)."""
    out = []
    for p in points:
        best = None
        for t in tree.query(p, k=12)[1]:
            a, b, c = P[tris[t]]
            bary, q = _closest_point(p, a, b, c)
            d = np.linalg.norm(p - q)
            if best is None or d < best[0]:
                best = (d, t, bary)
        out.append(best)
    return out


def _closest_point(p, a, b, c):
    """Closest point on triangle abc to p (Ericson), with its barycentric coordinates."""
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = ab @ ap, ac @ ap
    if d1 <= 0 and d2 <= 0:
        return (1, 0, 0), a
    bp = p - b
    d3, d4 = ab @ bp, ac @ bp
    if d3 >= 0 and d4 <= d3:
        return (0, 1, 0), b
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return (1 - v, v, 0), a + v * ab
    cp = p - c
    d5, d6 = ab @ cp, ac @ cp
    if d6 >= 0 and d5 <= d6:
        return (0, 0, 1), c
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return (1 - w, 0, w), a + w * ac
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return (0, 1 - w, w), b + w * (c - b)
    denom = 1 / (va + vb + vc)
    v, w = vb * denom, vc * denom
    return (1 - v - w, v, w), a + ab * v + ac * w


# ---------------------------------------------------------------- the CBBE side

class Cbbe:
    def __init__(self, nif, shape="CBBE"):
        self.skin = SkinnedShape(nif, shape)
        s = self.skin
        d = s.data
        self.ntri = struct.unpack_from("<I", d, 108)[0]
        self.tri_at = 118 + s.nvert * s.stride
        self.tris = np.array(struct.unpack_from("<%dH" % (3 * self.ntri), d, self.tri_at)).reshape(-1, 3)
        self.P = np.array([s.position(v) for v in range(s.nvert)], dtype=float)
        self.UV = np.array([struct.unpack_from("<2e", d, 118 + v * s.stride + 8) for v in range(s.nvert)], dtype=float)
        self.N = np.array([[x / 255.0 * 2 - 1 for x in struct.unpack_from("<3B", d, 118 + v * s.stride + 12)]
                           for v in range(s.nvert)])
        # the frame its normal map is read in: "tangent" (dP/dv) and bitangent (x a half float after the position,
        # y and z the fourth bytes of the normal and tangent)
        self.Tan = np.array([[x / 255.0 * 2 - 1 for x in struct.unpack_from("<3B", d, 118 + v * s.stride + 16)]
                             for v in range(s.nvert)])
        self.Bit = np.array([(struct.unpack_from("<e", d, 118 + v * s.stride + 6)[0],
                              d[118 + v * s.stride + 15] / 255.0 * 2 - 1, d[118 + v * s.stride + 19] / 255.0 * 2 - 1)
                             for v in range(s.nvert)])
        self.W = [dict((s.names[k], w) for k, w in s.influences(v)) for v in range(s.nvert)]
        self.ids = weld(self.P)

    def remove_triangles(self, drop):
        """Cut triangles out of the CBBE shape (vertices stay: the .osd slider data indexes them)."""
        keep = np.ones(self.ntri, bool)
        keep[list(drop)] = False
        s, d = self.skin, self.skin.data
        dsize = struct.unpack_from("<I", d, 114)[0]
        tail = bytes(d[118 + dsize:])
        tris = self.tris[keep]
        removed_before = np.concatenate([[0], np.cumsum(~keep)])
        head = bytearray(d[:self.tri_at])
        struct.pack_into("<I", head, 108, len(tris))
        struct.pack_into("<I", head, 114, s.nvert * s.stride + 6 * len(tris))
        new_tail = bytearray(tail)
        if len(tail) >= 12:   # BSSubIndexTriShape segments: primitive counts and ranges
            nprim, nseg, _ = struct.unpack_from("<III", tail, 0)
            struct.pack_into("<I", new_tail, 0, len(tris))
            p = 12
            for _ in range(nseg):
                start, cnt, _, nsub = struct.unpack_from("<IIII", tail, p)
                t0, t1 = start // 3, start // 3 + cnt
                struct.pack_into("<II", new_tail, p, 3 * (t0 - removed_before[t0]), cnt - (removed_before[t1] - removed_before[t0]))
                if nsub:
                    raise ValueError("sub-segments are not handled")
                p += 16
        s.data = head + struct.pack("<%dH" % (3 * len(tris)), *tris.ravel()) + new_tail
        self.tris, self.ntri, self.tri_at = tris, len(tris), self.tri_at


# ---------------------------------------------------------------- the transplant

def fit(gt_body, cbbe):
    G = np.array(gt_body.positions)
    d = np.linalg.norm(G - SLIT_GT, axis=1)
    src = G[(d > FIT_RING[0]) & (d < FIT_RING[1])]
    mid = cbbe.P[np.abs(cbbe.P[:, 0]) < 0.05]
    band = mid[(mid[:, 2] > -62) & (mid[:, 2] < -48)]
    crotch = band[np.argmin(band[:, 2])]
    tree = cKDTree(cbbe.P)
    s, R, tr = 1.0, np.eye(3), crotch - np.array(SLIT_GT)
    for _ in range(60):
        moved = s * src @ R.T + tr
        dist, idx = tree.query(moved)
        keep = dist < np.percentile(dist, 90)
        s, R, tr = umeyama(src[keep], cbbe.P[idx[keep]])
    dist, _ = tree.query(s * src @ R.T + tr)
    return (lambda p: s * np.asarray(p, float) @ R.T + tr), (s, R, tr, float(np.median(dist)), float(dist.max()))


def uv_islands(tris, nvert):
    """CBBE splits vertices along its UV seams, so its UV islands are its triangles connected by vertex index."""
    parent = list(range(nvert))

    def root(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in tris:
        ra = root(a)
        parent[root(b)] = ra
        parent[root(c)] = ra
    return np.array([root(t[0]) for t in tris])


def split_by_uv_island(full, ctree, cent, m):
    """UVs per triangle corner through the UV map of the CBBE triangle under the triangle's centre (extended
    across its plane), and vertices split where neighbouring triangles get different UVs: CBBE's crotch is
    cut along the midline in its UV layout and mirrored there, so one UV per vertex smears the texture across
    the seam and garbles tangents."""
    P, T = m["P"], m["tris"]
    island = uv_islands(full.tris, len(full.P))
    vert_island = np.full(len(full.P), -1)
    for t, tri in enumerate(full.tris):
        vert_island[tri] = island[t]
    own = [hit[1] for hit in closest_on_triangles(P, full.P, full.tris, ctree, cent)]   # each vertex's own triangle
    corner_uv = np.zeros((len(T), 3, 2))
    # the frame the body's normal map is read in, taken with the UVs from the same CBBE surface point, so the patch
    # reads the skin's normal map as the body does
    corner_tan, corner_bit = np.zeros((len(T), 3, 3)), np.zeros((len(T), 3, 3))
    def sample(t, bary):
        bary = np.clip(bary, 0, None)
        bary = bary / bary.sum()
        vs = full.tris[t]
        return bary @ full.UV[vs], bary @ full.Tan[vs], bary @ full.Bit[vs]
    centres = P[T].mean(1)
    for k, (tri, hit) in enumerate(zip(T, closest_on_triangles(centres, full.P, full.tris, ctree, cent))):
        home_t = hit[1]
        edge = [v for v in tri if v in m["fixed_normals"]]
        if edge:
            # touching the hole's edge: the UV island of the CBBE vertex it shares, so the join has no UV jump (where
            # CBBE's own UV seam runs into the hole, the jump then lies inside the patch, on CBBE's seam line)
            want = vert_island[m["src"][edge[0]][0][0]]
            if island[home_t] != want:
                home_t = next((t for t in ctree.query(centres[k], k=60)[1] if island[t] == want), home_t)
        a, b, c = full.P[full.tris[home_t]]
        E = np.array([b - a, c - a]).T                    # 3x2: the triangle's plane
        uv = full.UV[full.tris[home_t]]
        home = island[home_t]
        for j, v in enumerate(tri):
            l = np.linalg.lstsq(E, P[v] - a, rcond=None)[0]
            corner_uv[k, j] = uv[0] + l[0] * (uv[1] - uv[0]) + l[1] * (uv[2] - uv[0])
            _, corner_tan[k, j], corner_bit[k, j] = sample(home_t, np.array([1 - l[0] - l[1], l[0], l[1]]))
            if v in m["fixed_normals"]:
                # the hole's edge: exactly the CBBE vertex there, its copy in this triangle's UV island (CBBE splits
                # its crotch midline in UV, so one position can carry two UVs and two frames)
                cv = m["src"][v][0][0]
                dups = [d for d in np.nonzero(full.ids == full.ids[cv])[0] if vert_island[d] == home]
                if dups:
                    corner_uv[k, j], corner_tan[k, j], corner_bit[k, j] = full.UV[dups[0]], full.Tan[dups[0]], full.Bit[dups[0]]
                    continue
            if island[own[v]] != home:
                # this corner lies across a UV seam from the triangle: extended through the island it would land
                # in the texture's blank padding (pale wedges), so it takes the island's nearest point instead
                best = None
                for t in ctree.query(P[v], k=40)[1]:
                    if island[t] == home:
                        bary, q = _closest_point(P[v], *full.P[full.tris[t]])
                        dist = np.linalg.norm(P[v] - q)
                        if best is None or dist < best[0]:
                            best = (dist, t, np.asarray(bary))
                if best is not None and best[0] < 1.0:
                    corner_uv[k, j], corner_tan[k, j], corner_bit[k, j] = sample(best[1], best[2])
    # one vertex per UV chart a vertex touches: its corners' UVs averaged where they agree (within SAME_UV)
    corners = {}
    for k, tri in enumerate(T):
        for c, v in enumerate(tri):
            corners.setdefault(v, []).append((k, c))
    new_T = np.zeros_like(T)
    old, uvs, tans, bits = [], [], [], []
    for v, cs in corners.items():
        groups = []
        for k, c in cs:
            uv = corner_uv[k, c]
            for g in groups:
                if np.abs(g[0] / len(g[1]) - uv).max() < SAME_UV:
                    g[0] += uv
                    g[1].append((k, c))
                    break
            else:
                groups.append([uv.copy(), [(k, c)]])
        for total, members in groups:
            for k, c in members:
                new_T[k, c] = len(old)
            old.append(v)
            uvs.append(total / len(members))
            tans.append(sum(corner_tan[k, c] for k, c in members))
            bits.append(sum(corner_bit[k, c] for k, c in members))
    return dict(P=P[old], UV=np.array(uvs), tris=new_T, frame_T=np.array(tans), frame_B=np.array(bits),
                weights=[m["weights"][v] for v in old], src=[m["src"][v] for v in old],
                fixed_normals={i: m["fixed_normals"][v] for i, v in enumerate(old) if v in m["fixed_normals"]},
                blend_normals={i: m["blend_normals"][v] for i, v in enumerate(old) if v in m["blend_normals"]})


def is_genital(bone):
    return bone.startswith(GENITAL_PREFIXES)


def anchor(bone):
    """A soft region's physics bone (<bone>_SB) or dent bone (<bone>_SBnn) -> the skeleton bone it hangs from."""
    m = re.match(r"(.+)_SB\d*$", bone)
    return m.group(1) if m else bone


def blend_weights(dicts, bary):
    out = {}
    for dct, b in zip(dicts, bary):
        for k, w in dct.items():
            out[k] = out.get(k, 0) + w * b
    return out


def pull_toward(w, gw, pull):
    """w with its genital bones' (gw's) total share pulled `pull` of the way to 1, their mix kept, the rest scaled down"""
    g = sum(gw.values())
    if g <= 0 or pull <= 0 or g >= 1:
        return w
    total = sum(w.values())
    share = g + (total - g) * pull
    return {b: x * ((share / g) if b in gw else (total - share) / (total - g)) for b, x in w.items()}


def smooth_fade(d, width):
    """1 at d = 0, easing to 0 at d = width"""
    x = min(1.0, max(0.0, d / width))
    return 1 - x * x * (3 - 2 * x)


def top4(w):
    items = sorted(((k, v) for k, v in w.items() if v > 1e-4), key=lambda kv: -kv[1])[:4]
    total = sum(v for _, v in items)
    return {k: v / total for k, v in items}


def anal_frame(P, mouth, toward_vagina, left_point):
    """The anal canal's axis (pointing in) and the two directions across it: up (toward the vagina) and left"""
    _, _, vt = np.linalg.svd(P - P.mean(0))
    ax = vt[0] if (P.mean(0) - mouth) @ vt[0] > 0 else -vt[0]
    up = toward_vagina - mouth
    up = up - (up @ ax) * ax
    up /= np.linalg.norm(up)
    left = np.cross(ax, up)
    if (left_point - mouth) @ left < 0:
        left = -left
    return ax, up, left


def across(deg, up, left):
    a = math.radians(deg)
    return math.cos(a) * up + math.sin(a) * left


def lengthen_anal_canal(P, mouth, ax):
    """GT's anal canal (positions P) past ANAL_KEEP stretched to ANAL_DEPTH and made ANAL_RADIUS round, closing at the
    end as GT's does; unchanged up to ANAL_KEEP, so its rim stays on the skin. Returns the new positions and the
    canal's centre (off the axis through the mouth) at ANAL_KEEP, which the tube keeps."""
    d = (P - mouth) @ ax
    perp = P - mouth - np.outer(d, ax)
    dmax = d.max()
    mids, cen, rad = [], [], []
    edges = np.linspace(d.min(), dmax, 11)
    for lo, hi in zip(edges[:-1], edges[1:]):
        sel = (d >= lo) & (d <= hi)
        if sel.sum() >= 3:
            c = perp[sel].mean(0)
            mids.append(d[sel].mean()); cen.append(c); rad.append(np.linalg.norm(perp[sel] - c, axis=1).mean())
    mids, cen, rad = np.array(mids), np.array(cen), np.array(rad)
    c_at = lambda x: np.stack([np.interp(x, mids, cen[:, i]) for i in range(3)], -1)
    r_at = lambda x: np.interp(x, mids, rad)
    c_keep = c_at(ANAL_KEEP)
    deep = d > ANAL_KEEP
    t = (d[deep] - ANAL_KEEP) / (dmax - ANAL_KEEP)
    depth = ANAL_KEEP + t * (ANAL_DEPTH - ANAL_KEEP)
    target = np.interp(t, [0, 0.12, 0.8, 1], [r_at(ANAL_KEEP), ANAL_RADIUS, ANAL_RADIUS, ANAL_RADIUS * rad[-1] / rad.max()])
    out = P.copy()
    out[deep] = mouth + np.outer(depth, ax) + c_keep + (perp[deep] - c_at(d[deep])) * (target / r_at(d[deep]))[:, None]
    return out, c_keep


def ring_weights(p, mouth, ax, up, left, centre, rings):
    """rings: [(depth, [(bone, angle)])], deepening. The two rings either side of p's depth share it linearly, and
    each ring's share goes to the two bones either side of p's angle round the canal (bones 90 degrees apart)"""
    d = (p - mouth) @ ax
    r = p - mouth - d * ax - centre
    theta = math.degrees(math.atan2(r @ left, r @ up))
    depths = [x for x, _ in rings]
    if d <= depths[0]:
        mix = [(0, 1.0)]
    elif d >= depths[-1]:
        mix = [(len(rings) - 1, 1.0)]
    else:
        i = int(np.searchsorted(depths, d)) - 1
        f = (d - depths[i]) / (depths[i + 1] - depths[i])
        mix = [(i, 1 - f), (i + 1, f)]
    w = {}
    for i, share in mix:
        for bone, a in rings[i][1]:
            x = max(0.0, 1 - abs((theta - a + 180) % 360 - 180) / 90)
            if x * share > 0:
                w[bone] = w.get(bone, 0) + x * share
    return w


def tube(mouth, ax, up, left, centre, depths, radius, sides=16):
    """A tube's points and triangles: rings of `sides` points at each depth (none on a bone's own angle)"""
    angles = [360.0 * (k + 0.5) / sides for k in range(sides)]
    P = np.array([mouth + d * ax + centre + radius * across(a, up, left) for d in depths for a in angles])
    tris = []
    for j in range(len(depths) - 1):
        for k in range(sides):
            a, b = j * sides + k, j * sides + (k + 1) % sides
            tris += [(a, b, a + sides), (b, b + sides, a + sides)]
    return P, np.array(tris), angles


def build(cbbe_nif, gt_path, collision_path=None, skeleton_path=None):
    """Transplant into cbbe_nif (a nif_smp.Nif). Returns a report dict; the new shapes are returned as meshes
    {name: dict(P, UV, N, tangents, bitangents, tris, weights)} plus the genital bones {name: (position, rotation)}
    in skin space. With collision_path (GT's collision NIF), the COLLISION shapes come too, marked hidden; with
    skeleton_path (a GT NIF carrying its genital node tree), the spring anchors GT_ANCHORS and GT_PELVIS."""
    gt = SseNif(str(gt_path))
    body, vagina, anus = gt.shape("Softbody"), gt.shape("2Vagina"), gt.shape("2Anus")
    cbbe = Cbbe(cbbe_nif)
    full = Cbbe(cbbe_nif)    # an untouched copy: the surface the new shapes take UVs, weights and sliders from
    cent = full.P[full.tris].mean(1)
    ctree = cKDTree(cent)
    T, (s, R, tr, fit_med, fit_max) = fit(body, cbbe)
    anus_fo, front_fo = T(ANUS_GT), T(SLIT_FRONT_GT)

    # 2. the hole: triangles centred inside, then any left with two edges on the hole (the staircase a cut
    # along triangle edges leaves), so its edge runs smooth
    keep = seg_dist(cent, anus_fo, front_fo) >= R_HOLE
    near = seg_dist(cent, anus_fo, front_fo) < R_HOLE + 2
    while True:
        edges = {}
        for t in np.nonzero(keep)[0]:
            for x, y in ((0, 1), (1, 2), (2, 0)):
                e = tuple(sorted((cbbe.ids[cbbe.tris[t, x]], cbbe.ids[cbbe.tris[t, y]])))
                edges[e] = edges.get(e, 0) + 1
        ears = [t for t in np.nonzero(keep & near)[0]
                if sum(edges[tuple(sorted((cbbe.ids[cbbe.tris[t, x]], cbbe.ids[cbbe.tris[t, y]])))] == 1
                       for x, y in ((0, 1), (1, 2), (2, 0))) >= 2]
        bad = pinches(cbbe.tris[keep], cbbe.ids)
        pinched = [t for t in np.nonzero(keep & near)[0] if any(cbbe.ids[v] in bad for v in cbbe.tris[t])]
        if not ears and not pinched:
            break
        keep[ears + pinched] = False
    drop = np.nonzero(~keep)[0]
    cbbe.remove_triangles(drop)
    first = {}
    for v, i in enumerate(cbbe.ids):
        first.setdefault(i, v)
    hole = [l for l in boundary_loops(cbbe.tris, cbbe.ids)
            if seg_dist(cbbe.P[[first[i] for i in l]], anus_fo, front_fo).mean() < R_HOLE + 2.0]
    if len(hole) != 1:
        raise ValueError("the cut should leave one simple hole, found %d loops there" % len(hole))
    hole_v = [first[i] for i in hole[0]]

    # 3. GT's body patch: exactly the GT triangles lying over the CBBE triangles that were cut, so its edge hugs the
    # hole's within one (small) GT triangle and the bridge between them stays a thin flat strip; then trimmed
    # until its edges are three simple loops: outside, slit, anus
    G = np.array(body.positions)
    GT_tris = np.array(body.triangles)
    gt_cent = T(G[GT_tris].mean(1))
    cut = set(drop.tolist())
    cand = np.nonzero(seg_dist(gt_cent, anus_fo, front_fo) < R_HOLE + 1.5)[0]
    over = [k for k, (d, t, _) in zip(cand, closest_on_triangles(gt_cent[cand], full.P, full.tris, ctree, cent))
            if t in cut and d < OVER_DEPTH]
    ptris = GT_tris[over]
    gids = weld(G)
    while True:
        bad = pinches(ptris, gids)
        if not bad:
            break
        ptris = np.array([t for t in ptris if not any(gids[v] in bad for v in t)])
    pverts = np.unique(ptris)
    remap = {v: i for i, v in enumerate(pverts)}
    patch_P = T(G[pverts])
    patch_tris = np.vectorize(remap.get)(ptris)
    patch_ids = weld(patch_P)
    loops = boundary_loops(patch_tris, patch_ids)
    if len(loops) != 3:
        raise ValueError("the patch should have three edges (outside, slit, anus), found %d" % len(loops))
    outer = max(loops, key=lambda l: seg_dist(patch_P[[np.nonzero(patch_ids == i)[0][0] for i in l]], anus_fo, front_fo).mean())
    pfirst = {}
    for v, i in enumerate(patch_ids):
        pfirst.setdefault(i, v)
    outer_v = [pfirst[i] for i in outer]
    # ease the patch's outer band onto CBBE's surface (GT's inner thighs sit up to a unit off it) so the bridge
    # lies flat; measured from the outer edge, so the slit and anus seams with the genitals never move
    d, _ = cKDTree(patch_P[outer_v]).query(patch_P)
    edge_dist = d.copy()
    # and from the openings (the slit and anus edges), where the skin should follow the lip bones (OPENING_PULL)
    inner_v = [pfirst[i] for l in loops if l is not outer for i in l]
    opening_dist, _ = cKDTree(patch_P[inner_v]).query(patch_P)
    ease = np.clip(1 - d / EASE, 0, 1)
    ease = ease * ease * (3 - 2 * ease)
    blend_normals = {}
    for k in np.nonzero(ease > 0)[0]:
        _, t, bary = closest_on_triangles([patch_P[k]], full.P, full.tris, ctree, cent)[0]
        patch_P[k] += ease[k] * (np.asarray(bary) @ full.P[full.tris[t]] - patch_P[k])
        blend_normals[k] = (np.asarray(bary) @ full.N[full.tris[t]], ease[k])   # shading eases in too

    # bridge: CBBE's hole edge to the patch's outer edge
    Pv = list(patch_P) + [cbbe.P[v] for v in hole_v]
    hole_local = list(range(len(patch_P), len(Pv)))

    # Stitch in 3D: the crotch curves up to the pubis and the butt, so no plane unrolls it and pairing by angle or
    # by a planar parameter folds the bridge. Start at the closest pair, run both edges the same way, and at each
    # step take the shorter diagonal.
    X = np.array(Pv)
    A, B = list(outer_v), list(hole_local)
    j0 = int(np.argmin(np.linalg.norm(X[B] - X[A[0]], axis=1)))
    B = B[j0:] + B[:j0]
    def heading(loop):
        """where the loop goes over its first unit of length (the two edges differ in point density)"""
        walked, k = 0.0, 0
        while walked < 1.0 and k + 1 < len(loop):
            walked += np.linalg.norm(X[loop[k + 1]] - X[loop[k]])
            k += 1
        return X[loop[k]] - X[loop[0]]
    if heading(A) @ heading(B) < 0:
        B = B[:1] + B[1:][::-1]
    def share(loop):
        """each point's share of the way round its loop (both start at the closest pair, run the same way)"""
        seg = np.linalg.norm(X[loop] - X[loop[1:] + loop[:1]], axis=1)
        return np.concatenate([[0], np.cumsum(seg)[:-1]]) / seg.sum()
    ta, tb = share(A), share(B)
    bridge = []
    i = j = 0
    na, nb = len(A), len(B)
    while i < na or j < nb:
        a, b = A[i % na], B[j % nb]
        an, bn = A[(i + 1) % na], B[(j + 1) % nb]
        next_a = ta[i + 1] if i + 1 < na else 1.0
        next_b = tb[j + 1] if j + 1 < nb else 1.0
        if j >= nb or (i < na and next_a <= next_b):
            bridge.append((a, an, b)); i += 1
        else:
            bridge.append((a, bn, b)); j += 1
    vulva_tris = np.vstack([patch_tris, np.array(bridge)])
    vulva_P = np.array(Pv)

    # orient: the patch's winding should give outward normals like CBBE's
    tree_c = cKDTree(cbbe.P)
    def outward(P, tris):
        n = np.cross(P[tris[:, 1]] - P[tris[:, 0]], P[tris[:, 2]] - P[tris[:, 0]])
        c = P[tris].mean(1)
        return np.sum(n * cbbe.N[tree_c.query(c)[1]], axis=1)
    if np.median(outward(vulva_P, patch_tris)) < 0:
        vulva_tris[:len(patch_tris)] = vulva_tris[:len(patch_tris)][:, ::-1]
    nb = len(patch_tris)
    flip = outward(vulva_P, vulva_tris[nb:]) < 0
    vulva_tris[nb:][flip] = vulva_tris[nb:][flip][:, ::-1]

    # 4. the canals (GT's own UVs)
    gen_P = np.vstack([T(vagina.positions), T(anus.positions)])
    gen_UV = np.vstack([vagina.uvs, anus.uvs]).astype(float)
    gen_tris = np.vstack([np.array(vagina.triangles), np.array(anus.triangles) + len(vagina.positions)])
    # the rims the genitals share with the patch take the patch's exact positions: FO4 stores positions as half
    # floats (1/32 steps out here), and twins a hair apart would round to different steps and open the seam
    gen_ids = weld(gen_P)
    rim = np.isin(gen_ids, [i for l in boundary_loops(gen_tris, gen_ids) for i in l])
    rim_d, rim_twin = cKDTree(patch_P).query(gen_P)
    snap = rim & (rim_d < 0.1)
    gen_P[snap] = patch_P[rim_twin[snap]]
    gen_gtw = list(vagina.weights) + list(anus.weights)

    # the anal canal lengthened into a tube (ANAL_*), with its rings of bones: GT's anus ring (its four Anus2
    # bones, round the mouth) and ours deeper in
    n_vagina = len(vagina.positions)
    anus_bones = {b: T(anus.bone_position(b)) for b in anus.bone_names if is_genital(b)}
    anus2 = {b: p for b, p in anus_bones.items() if b.endswith("Anus2")}
    mouth = np.mean(list(anus2.values()), 0)
    ax, up, left = anal_frame(gen_P[n_vagina:], mouth, gen_P[:n_vagina].mean(0), anus2["NPC LT Anus2"])
    if (((gen_P[n_vagina:] - mouth) @ ax)[snap[n_vagina:]] > ANAL_KEEP).any():
        raise ValueError("the anal canal's rim lies deeper than ANAL_KEEP")
    old_depth = (gen_P[n_vagina:] - mouth) @ ax
    gen_P[n_vagina:], anal_centre = lengthen_anal_canal(gen_P[n_vagina:], mouth, ax)
    def angle_of(p):
        r = p - mouth - ((p - mouth) @ ax) * ax - anal_centre
        return math.degrees(math.atan2(r @ left, r @ up))
    anal_rings = [(float(np.mean([(p - mouth) @ ax for p in anus2.values()])),
                   [(b, angle_of(p)) for b, p in sorted(anus2.items())])]
    anal_bones = {}
    for i, depth in enumerate(ANAL_RINGS):
        ring = []
        for side, a in ANAL_SIDES:
            out = across(a, up, left)
            frame = np.column_stack([out, np.cross(ax, out), ax])    # x: straight out, z: into her
            name = "SB Anus%s%d" % (side, i + 1)
            anal_bones[name] = (mouth + depth * ax + anal_centre + ANAL_RADIUS * out, frame)
            anal_bones[name + " Base"] = anal_bones[name]
            ring.append((name, a))
        anal_rings.append((depth, ring))

    def surface(points):
        """UVs, body weights and the source (CBBE vertices, barycentrics) of the nearest CBBE surface point"""
        res = closest_on_triangles(points, full.P, full.tris, ctree, cent)
        uv, w, src = [], [], []
        for d, t, bary in res:
            vs = full.tris[t]
            uvs = full.UV[vs]
            if np.ptp(uvs[:, 1]) > 0.5:      # the crotch's UV seam: put all three on the same side
                uvs = uvs.copy()
                uvs[uvs[:, 1] < 0.5, 1] += 1.0
            uv.append(np.asarray(bary) @ uvs)
            w.append(blend_weights([full.W[v] for v in vs], bary))
            src.append((tuple(vs), tuple(bary)))
        return np.array(uv), w, src

    _, vul_body_w, vul_src = surface(vulva_P)
    for k, v in enumerate(hole_v):           # the seam follows CBBE's sliders exactly
        vul_src[len(patch_P) + k] = ((v,), (1.0,))
    gt_body_w = [body.weights[v] for v in pverts] + [None] * len(hole_v)
    vul_w = []
    for k in range(len(vulva_P)):
        if k >= len(patch_P):
            vul_w.append(cbbe.W[hole_v[k - len(patch_P)]])      # the seam: CBBE's own weights
            continue
        gw = {b: w for b, w in gt_body_w[k].items() if is_genital(b)}
        g = sum(gw.values())
        # CBBE's own weights (jiggle and dent bones included) at the outer seam, fading to the bones those hang
        # from toward the slit, where the canal (which must not jiggle with the belly or butt) joins
        fade = min(1.0, edge_dist[k] / WEIGHT_FADE)
        w = {}
        for b, x in vul_body_w[k].items():
            for bone, share in ((b, 1 - fade), (anchor(b), fade)):
                if share:
                    w[bone] = w.get(bone, 0) + x * (1 - g) * share
        w.update(gw)
        # near the openings, more of the skin rides on GT's lip and anus bones than GT gives it: GT lets those bones
        # travel up to 25 units, so its 5-30% shares moved the skin a lot; with SoftBody's 3-unit cap the entrance
        # barely opened while the bones behind it did. The genital share is pulled toward 1 by OPENING_PULL at the
        # edge, fading out over OPENING_FADE; GT's mix between its genital bones is kept
        vul_w.append(top4(pull_toward(w, gw, OPENING_PULL * smooth_fade(opening_dist[k], OPENING_FADE))))
    _, gen_body_w, gen_src = surface(gen_P)
    # The parts that must open with the penis ride on their bones like the outer skin does (GT leaves them mostly on
    # the pelvis, its bones travelling far enough to drag them anyway): the labia minora, vestibule and clitoris (2Vagina
    # off the canal walls, which GT already puts wholly on their ring bones) by OPENING_PULL, the anal canal by
    # ANUS_PULL. An anal-canal point GT left wholly on the pelvis takes its nearest anus bone. Inside the anus ring the
    # anal canal goes over to its rings of bones (ring_weights), by ANAL_BLEND deep.
    gen_w = []
    for k in range(len(gen_P)):
        if snap[k]:                          # the rims: exactly their twins' weights, or the seam opens
            gen_w.append(vul_w[rim_twin[k]])
            continue
        gw = {b: w for b, w in gen_gtw[k].items() if is_genital(b)}
        g = sum(gw.values())
        w = {}
        for b, x in gen_body_w[k].items():   # inside her, the body's share follows the jiggle bones' anchors
            b = anchor(b)
            w[b] = w.get(b, 0) + x * (1 - g)
        w.update(gw)
        if k < n_vagina:
            on_walls = sum(x for b, x in gw.items() if b.startswith("VaginaD")) >= 0.5
            pull = 0.0 if on_walls else OPENING_PULL
        else:
            pull = ANUS_PULL
            if g < 1e-4 and anus_bones:
                nearest = min(anus_bones, key=lambda b: np.linalg.norm(anus_bones[b] - gen_P[k]))
                w = {**w, nearest: 1e-3}
                gw = {nearest: 1e-3}
        w = top4(pull_toward(w, gw, pull))
        if k >= n_vagina:
            s = 1 - smooth_fade((gen_P[k] - mouth) @ ax, ANAL_BLEND) if (gen_P[k] - mouth) @ ax > 0 else 0.0
            if s > 0:
                rw = ring_weights(gen_P[k], mouth, ax, up, left, anal_centre, anal_rings)
                w = top4({b: (1 - s) * w.get(b, 0) + s * rw.get(b, 0) for b in set(w) | set(rw)})
        gen_w.append(w)

    # 5. genital bones, placed and turned by the fit (a skin bind's rotation is the bone's world rotation transposed)
    def bone_frame(shape, b):
        return T(shape.bone_position(b)), R @ np.array(shape.binds[b][0]).reshape(3, 3).T
    bones = {}
    for shape in (body, vagina, anus):
        for b in shape.bone_names:
            if is_genital(b):
                bones[b] = bone_frame(shape, b)
    bones.update(anal_bones)
    used = {b for w in vul_w + gen_w for b in w}
    report = dict(fit_scale=s, fit_rotation_deg=math.degrees(math.acos(max(-1, min(1, (np.trace(R) - 1) / 2)))),
                  fit_median=fit_med, fit_max=fit_max, cut_triangles=len(drop), hole_edge=len(hole_v),
                  patch_vertices=len(patch_P), patch_triangles=len(patch_tris), bridge_triangles=len(bridge),
                  bridge_flipped=int(flip.sum()), genital_vertices=len(gen_P), genital_triangles=len(gen_tris),
                  genital_bones=sorted(bones), bones_used=sorted(used - set(full.skin.names)),
                  fit_R=R, vagina_vertices=len(vagina.positions))
    vulva = split_by_uv_island(full, ctree, cent, dict(
        P=vulva_P, tris=vulva_tris, weights=vul_w, src=vul_src,
        fixed_normals={len(patch_P) + k: cbbe.N[v] for k, v in enumerate(hole_v)}, blend_normals=blend_normals))
    report["vulva_vertices_after_uv_split"] = len(vulva["P"])
    meshes = {VULVA: vulva,
              GENITALS: dict(P=gen_P, UV=gen_UV, tris=gen_tris, weights=gen_w, src=gen_src, fixed_normals={})}

    # 6. the collision pads: GT's geometry, each pad wholly on its own genital bone (GT's pelvis share dropped), as
    # the lip and anus proxies of a body that does collide in FO4 have it: a pad then moves as one with its bone, and
    # every one of its points collides; a point GT left on the pelvis alone takes its pad's main bone
    if collision_path:
        from scipy.sparse import coo_matrix
        from scipy.sparse.csgraph import connected_components
        coll = SseNif(str(collision_path))
        report["collision_triangles"] = {}
        for gt_name, name in COLLISION.items():
            shape = coll.shape(gt_name)
            P = T(shape.positions)
            tris = np.array(shape.triangles)
            e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
            _, piece = connected_components(coo_matrix((np.ones(len(e)), (e[:, 0], e[:, 1])), shape=(len(P), len(P))),
                                            directed=False)
            genital_w = [{b: w for b, w in gw.items() if is_genital(b)} for gw in shape.weights]
            main = {}
            for k, gw in enumerate(genital_w):
                for b, w in gw.items():
                    main.setdefault(piece[k], {}).setdefault(b, 0.0)
                    main[piece[k]][b] += w
            w_all = []
            for k, gw in enumerate(genital_w):
                if not gw:
                    if piece[k] not in main:
                        raise ValueError("%s: a pad with no genital bone" % gt_name)
                    gw = {max(main[piece[k]], key=main[piece[k]].get): 1.0}
                w_all.append(top4(gw))
            for b in shape.bone_names:
                if is_genital(b):
                    bones.setdefault(b, bone_frame(shape, b))
            _, _, src = surface(P)
            meshes[name] = dict(P=P, UV=np.zeros((len(P), 2)), tris=tris, weights=w_all,
                                src=src, fixed_normals={}, hidden=True)
            report["collision_triangles"][name] = len(shape.triangles)
        # the anal canal's pads: a short tube at each of our rings, each point wholly on its side's bone
        P, tris, w = [], [], []
        for depth, ring in anal_rings[1:]:
            p, t, angles = tube(mouth, ax, up, left, anal_centre, (depth - ANAL_PAD_HALF, depth, depth + ANAL_PAD_HALF),
                                ANAL_RADIUS)
            tris.append(t + sum(len(x) for x in P))
            P.append(p)
            for _ in range(3):
                for a in angles:
                    bone = min(ring, key=lambda ba: abs((a - ba[1] + 180) % 360 - 180))[0]
                    w.append({bone: 1.0})
        P, tris = np.vstack(P), np.vstack(tris)
        _, _, src = surface(P)
        meshes[ANAL_PADS] = dict(P=P, UV=np.zeros((len(P), 2)), tris=tris, weights=w, src=src, fixed_normals={},
                                 hidden=True)
        report["collision_triangles"][ANAL_PADS] = len(tris)

    # 7. the springs' anchors, from GT's node tree; its nodes must sit where its skin binds put the moving bones
    if skeleton_path:
        nodes = SseNif(str(skeleton_path)).node_transforms()
        frame = lambda n: (T(nodes[n][1]), R @ np.array(nodes[n][0]))
        report["anchor_mismatch"] = max(float(np.linalg.norm(frame(b)[0] - p)) for b, (p, _) in bones.items() if b in nodes)
        if report["anchor_mismatch"] > 0.05:
            raise ValueError("GT's node tree and skin binds disagree by %.3f" % report["anchor_mismatch"])
        for a in GT_ANCHORS:
            bones[a] = frame(a)
        bones[GT_PELVIS[1]] = frame(GT_PELVIS[0])
    return cbbe, meshes, bones, report


# ---------------------------------------------------------------- BodySlide slider data

def read_osd(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x00DSO":
        raise ValueError("%s: not an OSD file" % path)
    count = struct.unpack_from("<I", data, 8)[0]
    p, out = 12, {}
    for _ in range(count):
        n = data[p]
        name = data[p + 1:p + 1 + n].decode("latin-1")
        p += 1 + n
        k = struct.unpack_from("<H", data, p)[0]
        p += 2
        diffs = {}
        for _ in range(k):
            v, x, y, z = struct.unpack_from("<H3f", data, p)
            diffs[v] = (x, y, z)
            p += 14
        out[name] = diffs
    return out


def write_osd(path, records):
    out = bytearray(b"\x00DSO") + struct.pack("<II", 1, len(records))
    for name, diffs in records.items():
        n = name.encode("latin-1")
        out += struct.pack("<B", len(n)) + n + struct.pack("<H", len(diffs))
        for v, d in sorted(diffs.items()):
            out += struct.pack("<H3f", v, *d)
    open(path, "wb").write(out)


def slider_data(meshes, cbbe_osd):
    """The new shapes' slider offsets: each vertex moves as the CBBE surface point it was placed from.
    Returns {shape: {source record: (new record, {vertex: offset})}}."""
    src = read_osd(cbbe_osd)
    out = {}
    for shape, m in meshes.items():
        recs = {}
        for name, diffs in src.items():
            moved = {}
            for v, (vs, bary) in enumerate(m["src"]):
                d = [0.0, 0.0, 0.0]
                for s, b in zip(vs, bary):
                    if s in diffs:
                        for a in range(3):
                            d[a] += b * diffs[s][a]
                if max(abs(x) for x in d) > 1e-5:
                    moved[v] = tuple(d)
            if moved:
                recs[name] = (shape + name, moved)
        out[shape] = recs
    return out


# ---------------------------------------------------------------- writing the NIF

def normals(meshes):
    """Smooth normals over both shapes together (welded by position, so the slit's seam matches); CBBE's own
    normals on the hole's edge."""
    allP = np.vstack([m["P"] for m in meshes.values()])
    ids = weld(allP)
    acc = np.zeros((ids.max() + 1, 3))
    off = 0
    for m in meshes.values():
        P, T = m["P"], m["tris"]
        n = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        for c in range(3):
            np.add.at(acc, ids[T[:, c] + off], n)
        off += len(P)
    acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-12)
    off = 0
    for m in meshes.values():
        N = acc[ids[off:off + len(m["P"])]].copy()
        for v, (n, w) in m.get("blend_normals", {}).items():
            b = (1 - w) * N[v] + w * n / np.linalg.norm(n)
            N[v] = b / np.linalg.norm(b)
        for v, n in m["fixed_normals"].items():
            N[v] = n / np.linalg.norm(n)
        m["N"] = N
        off += len(m["P"])


def tangents(m):
    """FO4 stores dP/dv in the 'tangent' bytes and dP/du as the 'bitangent' (checked against CBBE)."""
    P, UV, T, N = m["P"], m["UV"], m["tris"], m["N"]
    du_acc, dv_acc = np.zeros_like(P), np.zeros_like(P)
    e1, e2 = P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]]
    d1, d2 = UV[T[:, 1]] - UV[T[:, 0]], UV[T[:, 2]] - UV[T[:, 0]]
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    ok = np.abs(r) > 1e-12
    r = np.where(ok, r, 1)[:, None]
    du = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r
    dv = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r
    du[~ok] = 0
    dv[~ok] = 0
    for c in range(3):
        np.add.at(du_acc, T[:, c], du)
        np.add.at(dv_acc, T[:, c], dv)
    def ortho(v):
        v = v - N * np.sum(v * N, axis=1, keepdims=True)
        bad = np.linalg.norm(v, axis=1) < 1e-9
        v[bad] = np.cross(N[bad], [1.0, 0, 0])
        return v / np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-12)
    if "frame_T" in m:
        # a shape textured from the body's own maps reads them in the body's frame (from the CBBE surface its UVs
        # came from), so its normal map shades as the body's does across the seam
        dv_acc, du_acc = m["frame_T"], m["frame_B"]
    V = ortho(dv_acc)
    # the bitangent is N x T, signed to follow dP/du (mirrored UVs flip it)
    U = np.cross(N, V)
    U *= np.where(np.sum(U * du_acc, axis=1, keepdims=True) < 0, -1.0, 1.0)
    m["U"], m["V"] = U, V


def _byte(x):
    return int(round((max(-1.0, min(1.0, x)) + 1) / 2 * 255))


def own_shader(nif, template, material=None, textures=None):
    """A copy of a BSLightingShaderProperty (FO4: shader type, then the material path as its name, the texture
    set ref at byte 40) with its own texture set; with a material, pointing at that material and textures.
    Every drawn shape needs its own: two drawn shapes sharing one hang the game when the body is put on."""
    data = bytearray(nif.blocks[template][1])
    if material is None:
        tex = nif.blocks[struct.unpack_from("<i", data, 40)[0]][1]
    else:
        struct.pack_into("<I", data, 4, nif.add_string(material))
        tex = struct.pack("<I", 10)
        for k in range(10):
            s = textures[k].encode("latin-1") if k < len(textures) else b""
            tex += struct.pack("<I", len(s)) + s
    struct.pack_into("<i", data, 40, len(nif.blocks))
    nif.blocks.append(["BSShaderTextureSet", tex])
    nif.blocks.append(["BSLightingShaderProperty", bytes(data)])
    return len(nif.blocks) - 1


def add_to_nif(nif, cbbe, meshes, bones, root=0, materials=None):
    """Commit the cut CBBE, add the genital bones under Pelvis_skin and the new shapes (hidden ones without a shader).
    materials: {shape: (material path, [texture paths])} for shapes that do not use the body's skin."""
    cbbe.skin.commit()
    skin = cbbe.skin
    k = skin.names.index("Pelvis_skin")
    _, prot, pt = skin.bind(k)
    pelvis = nif.find_node("Pelvis_skin")
    flags = nif.net(pelvis)[3].u32()
    bind = {n: skin.bind(i) for i, n in enumerate(skin.names)}
    for n in list(bind):                     # a jiggle bone's anchor binds as the jiggle bone does at rest
        if n.endswith("_SB"):
            bind.setdefault(n[:-3], bind[n])
    # Pelvis_skin's bind maps skin space to its own: v' = Pp v + pt. A child at skin position p, turned Rs in skin
    # space, sits at c = Pp p + pt turned L = Pp Rs (v_parent = L v_local), and binds as L^T Pp, L^T (pt - c)
    Pp = np.array(prot).reshape(3, 3)
    for name, (p, Rs) in sorted(bones.items()):
        c = Pp @ p + np.array(pt)
        L = Pp @ Rs
        node = len(nif.blocks)
        nif.blocks.append(["NiNode", struct.pack("<IIiI", nif.add_string(name), 0, -1, flags)
                           + struct.pack("<3f9ff", *c, *L.ravel(), 1) + struct.pack("<iI", -1, 0)])
        nif.add_child(pelvis, node)
        bind[name] = ((0.0, 0.0, 0.0, 2.0), tuple((L.T @ Pp).ravel()), tuple(L.T @ (np.array(pt) - c)))

    normals({name: m for name, m in meshes.items() if not m.get("hidden")})
    for name, m in meshes.items():           # each collision shape on its own: they touch nothing
        if m.get("hidden"):
            normals({name: m})
    src = nif.blocks[skin.index][1]
    desc = struct.unpack_from("<Q", src, 100)[0]
    shader, alpha = struct.unpack_from("<ii", src, 92)
    shape_flags = nif.net(skin.index)[3].u32()
    if "BSTriShape" not in nif.block_types:
        nif.block_types.append("BSTriShape")
    for name, m in meshes.items():
        tangents(m)
        # hidden (collision only) shapes have no shader, so they are never drawn: as a body whose hidden collision
        # proxies work in FO4 has them (an app-culled shape, as before, built in SMP but did not collide)
        shape_shader = -1 if m.get("hidden") else own_shader(nif, shader, *(materials or {}).get(name, ()))
        used = sorted({b for w in m["weights"] for b in w})
        index = {b: i for i, b in enumerate(used)}
        verts = bytearray()
        for v in range(len(m["P"])):
            p, n, u, t = m["P"][v], m["N"][v], m["U"][v], m["V"][v]
            w = sorted(m["weights"][v].items(), key=lambda kv: -kv[1])
            ws = [x for _, x in w] + [0.0] * (4 - len(w))
            ws = [struct.unpack("<e", struct.pack("<e", x))[0] for x in ws]
            ws[0] += 1 - sum(ws)
            ix = [index[b] for b, _ in w] + [0] * (4 - len(w))
            verts += struct.pack("<4e", p[0], p[1], p[2], u[0])
            verts += struct.pack("<2e", m["UV"][v][0], m["UV"][v][1])
            verts += struct.pack("<4B", _byte(n[0]), _byte(n[1]), _byte(n[2]), _byte(u[1]))
            verts += struct.pack("<4B", _byte(t[0]), _byte(t[1]), _byte(t[2]), _byte(u[2]))
            verts += struct.pack("<4e4B", *ws, *ix)
        geometry = bytes(verts) + struct.pack("<%dH" % m["tris"].size, *m["tris"].ravel())

        data_block = len(nif.blocks)
        bone_data = struct.pack("<I", len(used))
        for b in used:
            sphere, rot, t = bind[b]
            bone_data += struct.pack("<4f9f3ff", *sphere, *rot, *t, 1.0)
        nif.blocks.append(["BSSkin::BoneData", bone_data])
        skin_block = len(nif.blocks)
        refs = [nif.find_node(b) for b in used]
        nif.blocks.append(["BSSkin::Instance", struct.pack("<iiI", root, data_block, len(used))
                           + struct.pack("<%di" % len(refs), *refs) + struct.pack("<I", 0)])
        centre = m["P"].mean(0)
        radius = float(np.linalg.norm(m["P"] - centre, axis=1).max())
        shape = len(nif.blocks)
        data = (struct.pack("<IIiI", nif.add_string(name), 0, -1, shape_flags)
                + struct.pack("<3f9ff", 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1) + struct.pack("<i", -1)
                + struct.pack("<4f", *centre, radius)
                + struct.pack("<iiiQIHI", skin_block, shape_shader, alpha, desc, len(m["tris"]), len(m["P"]), len(geometry))
                + geometry)
        # body parts, as CBBE's and A-Body's are (collision proxies included): a BSSubIndexTriShape with CBBE's four
        # segments, every triangle in the last (a plain BSTriShape with its own skin shader gets no NPC skin tint)
        nt = len(m["tris"])
        segments = struct.pack("<III", nt, 4, 4) + struct.pack("<IIII", 0, 0, 0xFFFFFFFF, 0) * 3 \
            + struct.pack("<IIII", 0, nt, 0xFFFFFFFF, 0)
        nif.blocks.append(["BSSubIndexTriShape", data + segments])
        nif.add_child(root, shape)
