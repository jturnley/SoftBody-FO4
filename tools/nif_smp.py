"""Minimal Fallout 4 NIF (20.2.0.7, BS version 130) reader/patcher.

Only understands what is needed to:
  * list blocks, node hierarchy and shape names
  * attach an HDT-SMP physics XML to the root node via NiStringExtraData

All other block payloads are copied byte-for-byte.

Usage:
  python nif_smp.py info  <file.nif>
  python nif_smp.py tree  <file.nif> [filter]
  python nif_smp.py attach <in.nif> <out.nif> <xml path relative to Data>
"""
import struct
import sys

SMP_KEY = "HDT Skinned Mesh Physics Object"


class Reader:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def f32(self, n=1):
        v = struct.unpack_from("<%df" % n, self.data, self.pos)
        self.pos += 4 * n
        return v

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def export_string(self):
        n = self.u8()
        return self.raw(n).rstrip(b"\x00").decode("latin-1")

    def sized_string(self):
        n = self.u32()
        return self.raw(n).decode("latin-1")


def w_export_string(s):
    b = s.encode("latin-1") + b"\x00"
    return struct.pack("<B", len(b)) + b


def w_sized_string(s):
    b = s.encode("latin-1")
    return struct.pack("<I", len(b)) + b


class Nif:
    def __init__(self, path):
        data = open(path, "rb").read()
        r = Reader(data)
        line_end = data.index(b"\n")
        self.header_line = data[:line_end + 1]
        r.pos = line_end + 1
        self.version = r.u32()
        self.endian = r.u8()
        self.user_version = r.u32()
        num_blocks = r.u32()
        self.bs_version = r.u32()
        if self.version != 0x14020007 or self.bs_version != 130:
            raise ValueError("not a Fallout 4 NIF (version %08x, bsver %d)" % (self.version, self.bs_version))
        self.author = r.export_string()
        self.process_script = r.export_string()
        self.export_script = r.export_string()
        self.max_filepath = r.export_string()
        self.block_types = [r.sized_string() for _ in range(r.u16())]
        type_index = [r.u16() for _ in range(num_blocks)]
        sizes = [r.u32() for _ in range(num_blocks)]
        num_strings = r.u32()
        r.u32()  # max string length, recomputed on write
        self.strings = [r.sized_string() for _ in range(num_strings)]
        self.groups = [r.u32() for _ in range(r.u32())]
        self.blocks = []
        for ti, size in zip(type_index, sizes):
            self.blocks.append([self.block_types[ti & 0x7FFF], r.raw(size)])
        num_roots = r.u32()
        self.roots = [r.i32() for _ in range(num_roots)]
        if r.pos != len(data):
            raise ValueError("trailing data: parsed %d of %d bytes" % (r.pos, len(data)))

    # --- NiObjectNET / NiAVObject / NiNode helpers (FO4 layout) ---
    def string(self, idx):
        return None if idx == 0xFFFFFFFF else self.strings[idx]

    def net(self, i):
        """Return (name, extra_refs, controller, reader positioned after NiObjectNET)."""
        r = Reader(self.blocks[i][1])
        name = self.string(r.u32())
        extras = [r.i32() for _ in range(r.u32())]
        controller = r.i32()
        return name, extras, controller, r

    def node_children(self, i):
        name, extras, ctrl, r = self.net(i)
        r.u32()            # flags
        r.f32(3 + 9 + 1)   # translation, rotation, scale
        r.i32()            # collision object
        return [r.i32() for _ in range(r.u32())]

    def is_node(self, i):
        t = self.blocks[i][0]
        return t in ("NiNode", "BSFadeNode", "BSLeafAnimNode", "BSMasterParticleSystem", "BSMultiBoundNode", "BSOrderedNode", "BSValueNode", "BSTreeNode", "NiBillboardNode", "NiSwitchNode", "NiLODNode", "BSRangeNode", "BSDebrisNode", "BSBlastNode", "BSDamageStage")

    def is_net(self, i):
        t = self.blocks[i][0]
        return self.is_node(i) or t.endswith("TriShape")

    def string_extra(self, i):
        # NiExtraData is not an NiObjectNET: just Name, then the string data
        name, value = struct.unpack_from("<II", self.blocks[i][1], 0)
        return self.string(name), self.string(value)

    # --- patching ---
    def add_string(self, s):
        if s in self.strings:
            return self.strings.index(s)
        self.strings.append(s)
        return len(self.strings) - 1

    def attach_smp(self, xml_path, root=0):
        name, extras, ctrl, r = self.net(root)
        for e in extras:
            if e >= 0 and self.blocks[e][0] == "NiStringExtraData" and self.string_extra(e)[0] == SMP_KEY:
                # replace the existing path in place
                data = bytearray(self.blocks[e][1])
                struct.pack_into("<I", data, 4, self.add_string(xml_path))
                self.blocks[e][1] = bytes(data)
                return e
        new_index = len(self.blocks)
        self.blocks.append(["NiStringExtraData", struct.pack("<II", self.add_string(SMP_KEY), self.add_string(xml_path))])
        if "NiStringExtraData" not in self.block_types:
            self.block_types.append("NiStringExtraData")
        old = self.blocks[root][1]
        rest = old[8 + 4 * len(extras):]  # everything after the extra-data ref list
        new_extras = extras + [new_index]
        self.blocks[root][1] = old[:4] + struct.pack("<I", len(new_extras)) + struct.pack("<%di" % len(new_extras), *new_extras) + rest
        return new_index

    def find_node(self, name):
        for i in range(len(self.blocks)):
            if self.is_node(i) and self.net(i)[0] == name:
                return i
        raise KeyError(name)

    def split_physics_bone(self, name, suffix, root=0):
        """Give a skin bone a physics-only child, the way TWB / A-Body SMP bodies do.

        FO4 Faster HDT-SMP only simulates bones that are not in the actor skeleton,
        so the skin is moved onto `name + suffix`, parented to a new node `name`
        that keeps the original transform and binds to the skeleton bone in game.
        The skin keeps referencing the same block, so no weights change.
        """
        old = self.find_node(name)
        if old not in self.node_children(root):
            raise ValueError("%s is not a direct child of the root" % name)
        data = self.blocks[old][1]
        _, extras, ctrl, r = self.net(old)
        head = r.pos  # end of NiObjectNET
        flags = struct.unpack_from("<I", data, head)[0]
        transform = data[head + 4:head + 4 + 52]  # translation, rotation, scale
        identity = struct.pack("<3f9ff", 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1)
        # the original node becomes the physics bone at the parent's origin
        renamed = bytearray(data)
        struct.pack_into("<I", renamed, 0, self.add_string(name + suffix))
        renamed[head + 4:head + 4 + 52] = identity
        self.blocks[old][1] = bytes(renamed)
        # new parent with the original name and transform
        parent = len(self.blocks)
        self.blocks.append(["NiNode", struct.pack("<IIiI", self.add_string(name), 0, -1, flags) + transform + struct.pack("<iI1i", -1, 1, old)])
        # swap the child reference on the root
        rdata = bytearray(self.blocks[root][1])
        _, rextras, _, rr = self.net(root)
        children_at = rr.pos + 4 + 52 + 4 + 4  # flags, transform, collision, count
        children = self.node_children(root)
        struct.pack_into("<i", rdata, children_at + 4 * children.index(old), parent)
        self.blocks[root][1] = bytes(rdata)
        return parent

    def add_child(self, parent, child):
        data = self.blocks[parent][1]
        _, _, _, r = self.net(parent)
        count_at = r.pos + 4 + 52 + 4  # flags, transform, collision
        n = struct.unpack_from("<I", data, count_at)[0]
        end = count_at + 4 + 4 * n
        self.blocks[parent][1] = data[:count_at] + struct.pack("<I", n + 1) + data[count_at + 4:end] + struct.pack("<i", child) + data[end:]

    def add_hidden_twin(self, shape, twin_name, root=0):
        """Add a culled one-triangle copy of a skinned shape under the root.

        FO4 renames the geometry of a single-shape armor to "<ARMA>[sex]/<ARMO>"
        when it attaches it; with a second geometry it keeps the NIF shape
        names, which is what SMP mesh descriptors match on. The twin gets its
        own skin instance and bone data (copies) and shares the shader.
        """
        i = next(k for k in range(len(self.blocks)) if self.blocks[k][0].endswith("TriShape") and self.net(k)[0] == shape)
        data = self.blocks[i][1]
        _, extras, ctrl, r = self.net(i)
        flags = r.u32()
        transform = r.raw(52)
        r.i32()  # collision
        sphere = r.raw(16)
        skin, shader, alpha = r.i32(), r.i32(), r.i32()
        desc = struct.unpack_from("<Q", data, r.pos)[0]
        r.pos += 8
        r.u32(); r.u16(); r.u32()  # triangles, vertices, data size
        stride = (desc & 0xF) * 4
        verts = r.raw(3 * stride)
        if self.blocks[skin][0] != "BSSkin::Instance":
            raise ValueError("%s is not skinned" % shape)
        bone_data = struct.unpack_from("<i", self.blocks[skin][1], 4)[0]
        new_data = len(self.blocks)
        self.blocks.append(["BSSkin::BoneData", self.blocks[bone_data][1]])
        inst = bytearray(self.blocks[skin][1])
        struct.pack_into("<i", inst, 4, new_data)
        new_skin = len(self.blocks)
        self.blocks.append(["BSSkin::Instance", bytes(inst)])
        geometry = verts + struct.pack("<3H", 0, 1, 2)
        twin = (struct.pack("<IIiI", self.add_string(twin_name), 0, -1, flags | 1)  # bit 0: app culled
                + transform + struct.pack("<i", -1) + sphere
                + struct.pack("<iiiQIHI", new_skin, shader, alpha, desc, 1, 3, len(geometry)) + geometry)
        if "BSTriShape" not in self.block_types:
            self.block_types.append("BSTriShape")
        twin_index = len(self.blocks)
        self.blocks.append(["BSTriShape", twin])
        self.add_child(root, twin_index)
        return twin_index

    def write(self, path):
        out = bytearray(self.header_line)
        out += struct.pack("<IBII", self.version, self.endian, self.user_version, len(self.blocks))
        out += struct.pack("<I", self.bs_version)
        for s in (self.author, self.process_script, self.export_script, self.max_filepath):
            out += w_export_string(s)
        out += struct.pack("<H", len(self.block_types))
        for t in self.block_types:
            out += w_sized_string(t)
        for t, _ in self.blocks:
            out += struct.pack("<H", self.block_types.index(t))
        for _, d in self.blocks:
            out += struct.pack("<I", len(d))
        out += struct.pack("<II", len(self.strings), max((len(s) for s in self.strings), default=0))
        for s in self.strings:
            out += w_sized_string(s)
        out += struct.pack("<I", len(self.groups))
        for g in self.groups:
            out += struct.pack("<I", g)
        for _, d in self.blocks:
            out += d
        out += struct.pack("<I", len(self.roots))
        for root in self.roots:
            out += struct.pack("<i", root)
        open(path, "wb").write(out)


def cmd_info(nif):
    print("blocks: %d  strings: %d  roots: %s" % (len(nif.blocks), len(nif.strings), nif.roots))
    for i, (t, d) in enumerate(nif.blocks):
        label = ""
        if nif.is_net(i):
            label = nif.net(i)[0] or ""
        if t == "NiStringExtraData":
            label = "%s = %s" % nif.string_extra(i)
        if nif.is_node(i) or t.endswith("TriShape") or t.endswith("ExtraData"):
            print("%4d %-24s %8d  %s" % (i, t, len(d), label))


def cmd_tree(nif, flt=None):
    def walk(i, depth, path):
        name = nif.net(i)[0]
        here = path + [name]
        if flt is None or any(flt.lower() in (p or "").lower() for p in here):
            print("  " * depth + "%s [%s]" % (name, nif.blocks[i][0]))
        if nif.is_node(i):
            for c in nif.node_children(i):
                if c >= 0:
                    walk(c, depth + 1, here)
    for root in nif.roots:
        walk(root, 0, [])


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, path = argv[1], argv[2]
    nif = Nif(path)
    if cmd == "info":
        cmd_info(nif)
    elif cmd == "tree":
        cmd_tree(nif, argv[3] if len(argv) > 3 else None)
    elif cmd == "attach":
        out, xml = argv[3], argv[4]
        idx = nif.attach_smp(xml)
        nif.write(out)
        check = Nif(out)
        print("attached block %d: %s = %s" % ((idx,) + check.string_extra(idx)))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
