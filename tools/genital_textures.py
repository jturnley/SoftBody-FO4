"""Textures and material for the transplanted genitals (shape SoftBodyGenitals).

  _d  GT's vagina.dds (BC7, 4096) resized, as DXT1
  _n  GT's vagina_msn.dds is model space (R=x, G=z, B=y, checked against the
      mesh's normals); FO4 wants tangent space, so it is re-baked over the
      ported mesh: x along dP/du, y along dP/dv, as BC5. The anus' UVs land
      where the map holds nothing that matches its surface, so it is flat.
  _s  FO4's specular/gloss pair (BC5): a constant wet sheen
  .bgsm  the game's own female skin material (read from Fallout4 - Materials.ba2
      on this PC) with these three textures swapped in
"""
import os
import struct
import zlib

import numpy as np
from PIL import Image
from scipy import ndimage

SIZE = 2048
TEXTURES = ("SoftBodyFO4/Genitals_d.dds", "SoftBodyFO4/Genitals_n.dds", "SoftBodyFO4/Genitals_s.dds")
MATERIAL = r"Materials\SoftBodyFO4\Genitals.bgsm"
SKIN_BGSM = "materials/actors/character/basehumanfemale/basehumanfemaleskin.bgsm"
SPECULAR, GLOSS = 40, 100      # 0-255
# GT's vulva island in its texture (u 0.27-0.37, v 0.86-1.0, front at the top): the clitoral hood is its tip,
# skin-coloured from v 0.870 and fading back to GT's colours by 0.893, just in front of the clitoris (~0.895)
VULVA_ISLAND_U = (0.27, 0.37)
HOOD_V = (0.870, 0.893)
BLEED = 8                     # texels the islands are extended by
PAINTED = 245                 # a GT diffuse texel with any channel below this is painted (the rest is blank white)


def _blocks(image, pixel_format):
    """BCn block data for one image, compressed by Pillow (its own DDS header is dropped: it writes no mip count,
    a wrong linear size, and BC5 as DX10 'typeless' with an array size of 0, which BodySlide crashes on)."""
    import io
    buf = io.BytesIO()
    w, h = image.size
    padded = image if w >= 4 and h >= 4 else image.resize((max(w, 4), max(h, 4)))
    padded.convert("RGB" if pixel_format == "BC5" else "RGBA").save(buf, "DDS", pixel_format=pixel_format)
    data = buf.getvalue()
    head = 128 + (20 if data[84:88] == b"DX10" else 0)
    size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if pixel_format == "DXT1" else 16)
    return data[head:head + size]


def write_dds(path, image, fourcc):
    """A DDS the way CBBE's own are: legacy header (DXT1 or BC5U), full mip chain."""
    pixel_format = {b"DXT1": "DXT1", b"BC5U": "BC5"}[fourcc]
    w, h = image.size
    levels, body = [], b""
    level = image
    while True:
        body += _blocks(level, pixel_format)
        levels.append(level.size)
        if level.size == (1, 1):
            break
        level = level.resize((max(1, level.size[0] // 2), max(1, level.size[1] // 2)), Image.LANCZOS)
    block = 8 if fourcc == b"DXT1" else 16
    linear = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000       # caps, height, width, pixelformat, mipmapcount, linearsize
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, h, w, linear, 0, len(levels)) + b"\0" * 44
    header += struct.pack("<II4sIIIII", 32, 0x4, fourcc, 0, 0, 0, 0, 0)
    header += struct.pack("<IIIII", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)  # texture, complex, mipmap
    open(path, "wb").write(header + body)


def read_ba2(path, rel):
    """One file from a GNRL BA2 archive."""
    data = open(path, "rb").read()
    magic, _, kind, count, names_at = struct.unpack_from("<4sI4sIQ", data, 0)
    if magic != b"BTDX" or kind != b"GNRL":
        raise ValueError("%s: not a general BA2" % path)
    names, p = [], names_at
    for _ in range(count):
        n = struct.unpack_from("<H", data, p)[0]
        names.append(data[p + 2:p + 2 + n].decode("latin-1").replace("\\", "/").lower())
        p += 2 + n
    i = names.index(rel.lower())
    offset, packed, size = struct.unpack_from("<QII", data, 24 + 36 * i + 16)
    blob = data[offset:offset + (packed or size)]
    return zlib.decompress(blob) if packed else blob


def bgsm_fields(data):
    """Offsets in a BGSM v2: {"root": (start, end) of the root material path string, "skin_tint": the SkinTint
    flag's byte, "hair_tint": the hair tint colour's}. Fields after the nine texture paths, in order."""
    if data[:4] != b"BGSM" or struct.unpack_from("<I", data, 4)[0] != 2:
        raise ValueError("not a BGSM v2")
    p = 63
    for _ in range(9):
        p += 4 + struct.unpack_from("<I", data, p)[0]
    p += 1                  # EnableEditorAlphaRef
    p += 1 + 4 + 4          # RimLighting, RimPower, BackLightPower
    p += 1 + 4              # SubsurfaceLighting, its rolloff
    p += 1 + 12 + 4 + 4     # SpecularEnabled, SpecularColor, SpecularMult, Smoothness
    p += 4                  # FresnelPower
    p += 6 * 4              # wetness: spec scale, spec power scale, min var, env map scale, fresnel, metalness
    root = (p, p + 4 + struct.unpack_from("<I", data, p)[0])
    p = root[1]
    p += 1                  # AnisoLighting
    emit = data[p]
    p += 1 + (12 if emit else 0) + 4   # EmitEnabled, [EmittanceColor], EmittanceMult
    p += 3                  # ModelSpaceNormals, ExternalEmittance, BackLighting
    p += 9                  # ReceiveShadows, HideSecret, CastShadows, DissolveFade, AssumeShadowmask, Glowmap,
                            # EnvironmentMappingWindow, EnvironmentMappingEye, Hair
    hair_tint = p
    p += 12 + 1 + 1         # HairTintColor, Tree, Facegen
    return {"root": root, "skin_tint": p, "hair_tint": hair_tint}


def make_bgsm(template, textures):
    """The template material with its first textures replaced and its skin ties cut: no root material and no
    SkinTint flag. With SkinTint set, FO4 dresses the shape in the actor's own skin textures in game (a body part
    always, it seems), so the genitals showed plain skin; a working TWB vulva material has neither."""
    f = bgsm_fields(template)
    if struct.unpack_from("<3f", template, f["hair_tint"]) != (1.0, 1.0, 1.0):
        raise ValueError("unexpected BGSM layout: the hair tint should read 1,1,1")
    data = bytearray(template)
    data[f["skin_tint"]] = 0
    data[f["root"][0]:f["root"][1]] = struct.pack("<I", 1) + b"\0"
    p = 63
    out = bytearray(data[:p])
    for k in range(9):
        n = struct.unpack_from("<I", data, p)[0]
        old = bytes(data[p + 4:p + 4 + n])
        p += 4 + n
        new = (textures[k] + "\0").encode("latin-1") if k < len(textures) else old
        out += struct.pack("<I", len(new)) + new
    return bytes(out + data[p:])


def bake_normals(mesh, vagina_vertices, R, msn_path, diffuse_path):
    """Tangent-space normal map from GT's model-space one, over the ported mesh's UVs."""
    msn = np.asarray(Image.open(msn_path).convert("RGB"), dtype=np.float32) / 255 * 2 - 1
    msn = msn[..., [0, 2, 1]]                              # GT: R=x, G=z, B=y
    h, w = msn.shape[:2]
    # GT's texture is mostly blank (white) around its three painted islands, and some inner triangles are
    # mapped across the blank: there the model-space map holds one constant vector, which means nothing on
    # this surface, so those texels stay flat
    painted = np.asarray(Image.open(diffuse_path).convert("RGB").resize((w, h))).min(2) < PAINTED
    UV, T = mesh["UV"], mesh["tris"]
    N, U, V = mesh["N"], mesh["U"], mesh["V"]
    out = np.zeros((SIZE, SIZE, 3), np.float32)
    covered = np.zeros((SIZE, SIZE), bool)
    for tri in T:
        uv = UV[tri] * SIZE
        x0, y0 = np.floor(uv.min(0)).astype(int)
        x1, y1 = np.ceil(uv.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, SIZE - 1), min(y1, SIZE - 1)
        if x1 < x0 or y1 < y0:
            continue
        ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        px = np.stack([xs.ravel() + 0.5, ys.ravel() + 0.5], 1)
        a, b, c = uv
        m = np.array([b - a, c - a]).T
        if abs(np.linalg.det(m)) < 1e-9:
            continue
        l12 = np.linalg.solve(m, (px - a).T).T
        bary = np.column_stack([1 - l12.sum(1), l12])
        inside = (bary >= -1e-4).all(1)
        if not inside.any():
            continue
        bary, px = bary[inside], px[inside].astype(int)
        n = bary @ N[tri]
        n /= np.linalg.norm(n, axis=1, keepdims=True)
        ts = np.tile([0.0, 0.0, 1.0], (len(px), 1))
        uvp = bary @ UV[tri]
        sx = np.clip((uvp[:, 0] % 1) * w, 0, w - 1).astype(int)
        sy = np.clip((uvp[:, 1] % 1) * h, 0, h - 1).astype(int)
        use = painted[sy, sx] if tri.max() < vagina_vertices else np.zeros(len(px), bool)
        if use.any():
            n, b = n[use], bary[use]
            model = msn[sy[use], sx[use]] @ R.T            # GT space -> FO4 space
            u = b @ U[tri]
            v = b @ V[tri]
            u -= n * np.sum(u * n, 1, keepdims=True)
            u /= np.maximum(np.linalg.norm(u, axis=1, keepdims=True), 1e-9)
            v -= n * np.sum(v * n, 1, keepdims=True) + u * np.sum(v * u, 1, keepdims=True)
            v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-9)
            t = np.stack([np.sum(model * u, 1), np.sum(model * v, 1), np.sum(model * n, 1)], 1)
            t[:, 2] = np.maximum(t[:, 2], 0.05)
            ts[use] = t / np.linalg.norm(t, axis=1, keepdims=True)
        out[px[:, 1], px[:, 0]] = ts
        covered[px[:, 1], px[:, 0]] = True
    # bleed the islands a few texels outward so filtering at their edges does not pull in flat ones
    dist, idx = ndimage.distance_transform_edt(~covered, return_indices=True)
    bled = out[idx[0], idx[1]]
    near = dist <= BLEED
    out[near] = bled[near]
    out[~near] = (0.0, 0.0, 1.0)
    return out, covered.mean()


def skin_gain(diffuse, mesh, vulva, skin_path):
    """Per-channel gain (linear light) that brings GT's texture to this skin where the two shapes meet: GT
    painted it for Skyrim's skin, and FO4 skins are another tone."""
    from scipy.spatial import cKDTree
    skin = np.asarray(Image.open(skin_path).convert("RGB"), dtype=np.float32) / 255
    gt = np.asarray(diffuse, dtype=np.float32) / 255
    d, idx = cKDTree(vulva["P"]).query(mesh["P"])
    rim = np.nonzero(d < 1e-3)[0]                     # genital vertices on the seam with the vulva

    def sample(img, uv, painted_only):
        """5x5 texel means in linear light (GT's: painted texels only; its islands' margins are blank white)"""
        h, w = img.shape[:2]
        x = np.clip((uv[:, 0] % 1) * w, 2, w - 3).astype(int)
        y = np.clip((uv[:, 1] % 1) * h, 2, h - 3).astype(int)
        patch = np.stack([img[y + dy, x + dx] for dy in range(-2, 3) for dx in range(-2, 3)], 1) ** 2.2
        keep = (patch ** (1 / 2.2)).min(2) < PAINTED / 255 if painted_only else np.ones(patch.shape[:2], bool)
        sums = (patch * keep[..., None]).sum(1)
        n = keep.sum(1)
        return sums[n > 0] / n[n > 0, None], n > 0
    theirs, ok = sample(gt, mesh["UV"][rim], True)
    ours, _ = sample(skin, vulva["UV"][idx[rim]], False)
    return np.median(ours[ok], 0) / np.maximum(np.median(theirs, 0), 1e-4), int(ok.sum()), np.median(ours[ok], 0)


def skin_hood(lin, skin_colour, painted):
    """GT paints the clitoral hood (the vulva island's front tip, above the clitoris) in the pink of the
    clitoris and inner lips; it is skin, like the outer lips. Fade it to the skin's colour, keeping GT's shading
    (its light and dark, without its hue)."""
    v = (np.arange(SIZE) + 0.5) / SIZE
    u = (np.arange(SIZE) + 0.5) / SIZE
    w = np.clip((HOOD_V[1] - v) / (HOOD_V[1] - HOOD_V[0]), 0, 1)
    w = w * w * (3 - 2 * w)
    rows = (v >= HOOD_V[0] - 0.03) & (v < HOOD_V[1])
    cols = (u > VULVA_ISLAND_U[0]) & (u < VULVA_ISLAND_U[1])
    mask = painted[np.ix_(rows, cols)]
    region = lin[np.ix_(rows, cols)]
    tone = region.mean(2)
    shade = tone / max(float(np.median(tone[mask])), 1e-4)
    weight = (w[rows][:, None] * mask)[..., None]
    lin[np.ix_(rows, cols)] = region * (1 - weight) + shade[..., None] * skin_colour * weight
    return lin, float(mask.sum())


def write(out_root, mesh, vagina_vertices, R, gt_root, game_data, vulva, skin_path):
    tex_dir = os.path.join(out_root, "Textures", "SoftBodyFO4")
    os.makedirs(tex_dir, exist_ok=True)
    src = os.path.join(gt_root, r"textures\actors\character\female")
    diffuse = Image.open(os.path.join(src, "vagina.dds")).convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)
    gain, rim, skin_colour = skin_gain(diffuse, mesh, vulva, skin_path)
    painted = np.asarray(diffuse).min(2) < PAINTED
    lin = (np.asarray(diffuse, dtype=np.float32) / 255) ** 2.2 * gain
    lin, hood_texels = skin_hood(lin, skin_colour, painted)
    v0, v1 = int(HOOD_V[0] * SIZE), int((HOOD_V[0] + HOOD_V[1]) / 2 * SIZE)
    u0, u1 = int(VULVA_ISLAND_U[0] * SIZE), int(VULVA_ISLAND_U[1] * SIZE)
    hood_now = lin[v0:v1, u0:u1][painted[v0:v1, u0:u1]].mean(0)
    diffuse = Image.fromarray((np.clip(lin, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8))
    write_dds(os.path.join(tex_dir, "Genitals_d.dds"), diffuse, b"DXT1")
    normal, coverage = bake_normals(mesh, vagina_vertices, R, os.path.join(src, "vagina_msn.dds"),
                                    os.path.join(src, "vagina.dds"))
    rgb = np.zeros((SIZE, SIZE, 3), np.uint8)
    rgb[..., 0] = np.clip((normal[..., 0] + 1) / 2 * 255, 0, 255)
    rgb[..., 1] = np.clip((normal[..., 1] + 1) / 2 * 255, 0, 255)
    write_dds(os.path.join(tex_dir, "Genitals_n.dds"), Image.fromarray(rgb), b"BC5U")
    spec = np.zeros((SIZE, SIZE, 3), np.uint8)
    spec[..., 0], spec[..., 1] = SPECULAR, GLOSS
    write_dds(os.path.join(tex_dir, "Genitals_s.dds"), Image.fromarray(spec), b"BC5U")
    template = read_ba2(os.path.join(game_data, "Fallout4 - Materials.ba2"), SKIN_BGSM)
    mat = os.path.join(out_root, MATERIAL)
    os.makedirs(os.path.dirname(mat), exist_ok=True)
    open(mat, "wb").write(make_bgsm(template, TEXTURES))
    return dict(normal_coverage=coverage, material=MATERIAL, skin_gain=gain, rim_vertices=rim,
                hood_texels=hood_texels, hood_colour=hood_now, skin_colour=skin_colour)
