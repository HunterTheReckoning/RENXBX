#!/usr/bin/env python3
"""
Per-map memory budget for the Renegade Xbox port.

For each level, follows what the game preloads (Combat/combat.cpp):
  always.dep  (from Always2.dat, which the game searches before always.dat)
  <level>.dep (from the level's own .mix)
Each .dep lists .w3d model files (Combat/assetdep.cpp). This script opens those
models and sizes every mesh from the arrays the engine actually builds:

  Engine copy (ww3d2/meshgeometry.h, meshmatdesc.h), per vertex:
    position 12, normal 12 (if present), shade index 4 (if present),
    bone link 2 (skins), 8 per UV array, 4 per color array
  per triangle: indices 6, plane equation 16, surface type 1
  plus the collision tree (W3D_CHUNK_AABTREE), roughly its file size.

  PC renderer copy (ww3d2/dx8renderer.cpp, Define_FVF and Add_Mesh), rigid meshes:
    interleaved vertex: position 12, normal 12 (only if realtime-lit),
    diffuse 4, specular 4, 8 per UV array; plus 6 bytes of index per triangle.
  Skinned meshes are rebuilt each frame into a small shared buffer, so no copy.

Only the material set the engine loads is counted: lightmap multi-texture by
default, falling back like meshmdlio.cpp does (multi-pass, vertex, unlit).

Three totals per level:
  PC-style    engine copy + renderer copy (what a straight port would do)
  Xbox single engine copy only, drawn directly by the GPU, + 6 B/tri indices
  Xbox lean   as single, also dropping normals from prelit meshes, which the
              GPU never reads and collision doesn't use

Textures are summed the way the engine loads them (.dds first, then .tga).

Standalone: does not need any other file.

Usage:
  python renegade_mapbudget.py "C:/Path/To/Renegade/Data" [--levels "m*"] [--top 15]
                               [--prelit multitexture|multipass|vertex]

Estimates, not exact: models loaded later by scripts, and skeletons/animations
referenced by name from inside models, are not followed.
"""

import argparse
import fnmatch
import os
import struct
import sys

# Shared helpers (same as renegade_inventory.py, copied so this file stands alone)

ARCHIVE_EXTS = (".mix", ".dat", ".dbs")


FOURCC_BLOCK_BYTES = {b"DXT1": 8, b"DXT2": 16, b"DXT3": 16, b"DXT4": 16, b"DXT5": 16}


def read_mix(path):
    """Return list of (name, offset, size) or None if not a MIX1 archive."""
    with open(path, "rb") as f:
        head = f.read(12)
        if len(head) < 12 or head[:4] != b"MIX1":
            return None
        header_offset, names_offset = struct.unpack("<ii", head[4:12])

        f.seek(header_offset)
        (count,) = struct.unpack("<i", f.read(4))
        table = [struct.unpack("<III", f.read(12)) for _ in range(count)]

        f.seek(names_offset)
        (name_count,) = struct.unpack("<i", f.read(4))
        names = []
        for _ in range(name_count):
            (n,) = struct.unpack("<B", f.read(1))
            names.append(f.read(n).rstrip(b"\0").decode("latin-1"))

    if name_count != count:
        print(f"  warning: {os.path.basename(path)} has {count} entries but {name_count} names",
              file=sys.stderr)
    return [(names[i] if i < len(names) else f"<unnamed_{i}>", off, size)
            for i, (_crc, off, size) in enumerate(table)]


def dds_info(fh, offset):
    """Read width, height, mip count, fourCC from a DDS at offset."""
    fh.seek(offset)
    data = fh.read(128)
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    height, width = struct.unpack("<II", data[12:20])
    (mips,) = struct.unpack("<I", data[28:32])
    fourcc = data[84:88]
    return width, height, max(mips, 1), fourcc


def dxt_level_size(w, h, block_bytes):
    return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block_bytes


def engine_texture_bytes(width, height, mips, fourcc, reduction):
    """Mirror DDSFileClass: skip `reduction` top mips, then drop the two smallest."""
    block = FOURCC_BLOCK_BYTES.get(fourcc)
    if block is None:
        return None
    levels = mips
    r = reduction
    if levels > r:
        levels -= r
    else:
        r = levels - 1
        levels = 1
    levels = levels - 2 if levels > 2 else 1
    w, h = max(width >> r, 1), max(height >> r, 1)
    total = 0
    for _ in range(levels):
        total += dxt_level_size(w, h, block)
        w, h = max(w >> 1, 1), max(h >> 1, 1)
    return total


def next_pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def tga_info(fh, offset):
    """Return (width, height, bpp, has_alpha) from a TGA header at offset."""
    fh.seek(offset)
    h = fh.read(18)
    if len(h) < 18:
        return None
    width, height = struct.unpack("<HH", h[12:16])
    bpp = h[16]
    alpha_bits = h[17] & 0x0F
    if width == 0 or height == 0:
        return None
    return width, height, bpp, alpha_bits > 0 or bpp == 32


def tga_memory(width, height, has_alpha, reduction):
    """Estimate (as-loaded bytes, as-DXT bytes) with a full mip chain.
    The engine pads to power-of-two and loads TGAs as 32-bit."""
    w = max(next_pow2(width) >> reduction, 1)
    h = max(next_pow2(height) >> reduction, 1)
    loaded = dxt = 0
    block = 16 if has_alpha else 8
    while True:
        loaded += w * h * 4
        dxt += dxt_level_size(w, h, block)
        if w == 1 and h == 1:
            break
        w, h = max(w >> 1, 1), max(h >> 1, 1)
    return loaded, dxt


def mb(n):
    return f"{n / (1024 * 1024):8.2f} MB"

CHUNKID_FILE_LIST = 0x04020527

# W3D chunk IDs (ww3d2/w3d_file.h)
W3D_MESH = 0x00
W3D_VERTEX_NORMALS = 0x03
W3D_VERTEX_INFLUENCES = 0x0E
W3D_MESH_HEADER3 = 0x1F
W3D_VERTEX_SHADE_INDICES = 0x22
W3D_PRELIT_UNLIT = 0x23
W3D_PRELIT_VERTEX = 0x24
W3D_PRELIT_MULTI_PASS = 0x25
W3D_PRELIT_MULTI_TEXTURE = 0x26
W3D_TEXTURE_NAME = 0x32
W3D_DCG = 0x3B
W3D_DIG = 0x3C
W3D_SCG = 0x3E
W3D_STAGE_TEXCOORDS = 0x4A
W3D_AABTREE = 0x90
W3D_EMITTER_INFO = 0x503

FLAG_SKIN = 0x00020000
FLAG_GEOMETRY_TYPE_MASK = 0x00FF0000
FLAG_PRELIT_MASK = 0x0F000000
FLAG_PRELIT_UNLIT = 0x01000000
FLAG_PRELIT_VERTEX = 0x02000000
FLAG_PRELIT_MULTI_PASS = 0x04000000
FLAG_PRELIT_MULTI_TEXTURE = 0x08000000

PRELIT_FALLBACK = {
    "multitexture": [(FLAG_PRELIT_MULTI_TEXTURE, W3D_PRELIT_MULTI_TEXTURE),
                     (FLAG_PRELIT_MULTI_PASS, W3D_PRELIT_MULTI_PASS),
                     (FLAG_PRELIT_VERTEX, W3D_PRELIT_VERTEX)],
    "multipass": [(FLAG_PRELIT_MULTI_PASS, W3D_PRELIT_MULTI_PASS),
                  (FLAG_PRELIT_VERTEX, W3D_PRELIT_VERTEX)],
    "vertex": [(FLAG_PRELIT_VERTEX, W3D_PRELIT_VERTEX)],
}
PRELIT_MODE = "multitexture"  # Renegade's default (Commando/shutdown.cpp)


def chunks(data, start, end):
    """Yield (id, is_container, body_start, body_end) for chunks in a range."""
    pos = start
    while pos + 8 <= end:
        cid, csize = struct.unpack("<II", data[pos:pos + 8])
        size = csize & 0x7FFFFFFF
        body = pos + 8
        if body + size > end:
            return
        yield cid, bool(csize & 0x80000000), body, body + size
        pos = body + size


def scan_material(data, start, end, out):
    """Count UV arrays, color arrays and texture names in a material region."""
    for cid, container, b, e in chunks(data, start, end):
        if cid == W3D_STAGE_TEXCOORDS:
            out["uv"] += 1
        elif cid in (W3D_DCG, W3D_DIG):
            out["diffuse"] = True
        elif cid == W3D_SCG:
            out["specular"] = True
        elif cid == W3D_TEXTURE_NAME:
            name = data[b:e].split(b"\0")[0].decode("latin-1").strip()
            if name:
                out["textures"].add(name.lower())
        elif container:
            scan_material(data, b, e, out)


def parse_mesh(data, start, end, stats):
    attrs = verts = tris = 0
    has_normals = has_shade = has_infl = False
    aabtree = 0
    prelit = {}
    for cid, container, b, e in chunks(data, start, end):
        if cid == W3D_MESH_HEADER3 and e - b >= 48:
            (attrs,) = struct.unpack("<I", data[b + 4:b + 8])
            tris, verts = struct.unpack("<II", data[b + 40:b + 48])
        elif cid == W3D_VERTEX_NORMALS:
            has_normals = True
        elif cid == W3D_VERTEX_SHADE_INDICES:
            has_shade = True
        elif cid == W3D_VERTEX_INFLUENCES:
            has_infl = True
        elif cid == W3D_AABTREE:
            aabtree = e - b
        elif cid in (W3D_PRELIT_UNLIT, W3D_PRELIT_VERTEX,
                     W3D_PRELIT_MULTI_PASS, W3D_PRELIT_MULTI_TEXTURE):
            prelit[cid] = (b, e)

    mat = {"uv": 0, "diffuse": False, "specular": False, "textures": set()}
    is_prelit = bool(attrs & FLAG_PRELIT_MASK)
    if is_prelit:
        chosen = W3D_PRELIT_UNLIT
        for flag, cid in PRELIT_FALLBACK[PRELIT_MODE]:
            if attrs & flag and cid in prelit:
                chosen = cid
                break
        if chosen in prelit:
            scan_material(data, *prelit[chosen], mat)
    else:
        # Non-prelit: material chunks sit directly in the mesh. Skip prelit wrappers.
        for cid, container, b, e in chunks(data, start, end):
            if cid in (W3D_PRELIT_UNLIT, W3D_PRELIT_VERTEX,
                       W3D_PRELIT_MULTI_PASS, W3D_PRELIT_MULTI_TEXTURE):
                continue
            if cid == W3D_TEXTURE_NAME:
                name = data[b:e].split(b"\0")[0].decode("latin-1").strip()
                if name:
                    mat["textures"].add(name.lower())
            elif container:
                scan_material(data, b, e, mat)
            elif cid == W3D_STAGE_TEXCOORDS:
                mat["uv"] += 1
            elif cid in (W3D_DCG, W3D_DIG):
                mat["diffuse"] = True
            elif cid == W3D_SCG:
                mat["specular"] = True

    skin = (attrs & FLAG_GEOMETRY_TYPE_MASK) == FLAG_SKIN or has_infl
    colors = int(mat["diffuse"]) + int(mat["specular"])

    engine_v = 12 + (12 if has_normals else 0) + (4 if has_shade else 0) \
        + (2 if skin else 0) + 8 * mat["uv"] + 4 * colors
    engine = verts * engine_v + tris * (6 + 16 + 1) + aabtree

    needs_normals = not is_prelit
    if skin:
        renderer = 0
    else:
        stride = 12 + (12 if needs_normals else 0) + 4 * colors + 8 * mat["uv"]
        renderer = verts * stride + tris * 6

    lean_drop = 12 * verts if (is_prelit and has_normals and not skin) else 0

    stats["meshes"] += 1
    stats["verts"] += verts
    stats["tris"] += tris
    stats["pc"] += engine + renderer
    stats["single"] += engine + tris * 6
    stats["lean"] += engine + tris * 6 - lean_drop
    stats["textures"] |= mat["textures"]


def walk_w3d(data, start, end, stats):
    """Find meshes and emitters anywhere in a W3D file."""
    for cid, container, b, e in chunks(data, start, end):
        if cid == W3D_MESH and container:
            parse_mesh(data, b, e, stats)
        elif cid == W3D_EMITTER_INFO and e - b >= 260:
            name = data[b:b + 260].split(b"\0")[0].decode("latin-1").strip()
            if name:
                stats["textures"].add(name.lower())
        elif container:
            walk_w3d(data, b, e, stats)


def new_stats():
    return {"verts": 0, "tris": 0, "meshes": 0, "pc": 0, "single": 0, "lean": 0,
            "textures": set()}


def model_stats(archives, w3d_names, order, cache, per_model=None):
    total = new_stats()
    total.update({"w3d_bytes": 0, "anim_bytes": 0, "found": 0, "missing": []})
    seen = set()
    for name in w3d_names:
        # The engine skips models it has already loaded (Render_Obj_Exists).
        if name.lower() in seen:
            continue
        seen.add(name.lower())
        hit = archives.find(name, order)
        if not hit:
            total["missing"].append(name)
            continue
        arc, off, size = hit
        key = (arc, off)
        if key not in cache:
            stats = new_stats()
            walk_w3d(archives.read(arc, off, size), 0, size, stats)
            stats["size"] = size
            if stats["meshes"] == 0:
                # Skeletons, animations, emitters: memory ~ file size.
                for k in ("pc", "single", "lean"):
                    stats[k] = size
            cache[key] = stats
        s = cache[key]
        if per_model is not None:
            per_model.append((name, s))
        total["found"] += 1
        total["w3d_bytes"] += s["size"]
        if s["meshes"] == 0:
            total["anim_bytes"] += s["size"]
        for k in ("verts", "tris", "meshes", "pc", "single", "lean"):
            total[k] += s[k]
        total["textures"] |= s["textures"]
    return total


class ArchiveSet:
    """All archives, with lookup by lower-case filename in a chosen order."""

    def __init__(self, data_dir):
        self.data_dir = data_dir
        self.index = {}  # archive name -> {lower filename: (offset, size)}
        self.handles = {}
        for arc in sorted(os.listdir(data_dir)):
            if not arc.lower().endswith(ARCHIVE_EXTS):
                continue
            entries = read_mix(os.path.join(data_dir, arc))
            if entries is None:
                continue
            table = {}
            for name, off, size in entries:
                table.setdefault(os.path.basename(name.replace("\\", "/")).lower(), (off, size))
            self.index[arc] = table
        priority = ["always2.dat", "always.dbs", "always.dat", "always3.dat"]
        lower = {a.lower(): a for a in self.index}
        self.base_order = [lower[p] for p in priority if p in lower]
        self.base_order += [a for a in sorted(self.index) if a not in self.base_order]

    def order_for(self, level_arc=None):
        if level_arc:
            return [level_arc] + [a for a in self.base_order if a != level_arc]
        return self.base_order

    def find(self, filename, order):
        key = os.path.basename(filename.replace("\\", "/")).lower()
        for arc in order:
            hit = self.index[arc].get(key)
            if hit:
                return arc, hit[0], hit[1]
        return None

    def handle(self, arc):
        if arc not in self.handles:
            self.handles[arc] = open(os.path.join(self.data_dir, arc), "rb")
        return self.handles[arc]

    def read(self, arc, off, size):
        fh = self.handle(arc)
        fh.seek(off)
        return fh.read(size)


def parse_dep(data):
    """Return list of .w3d filenames from a .dep file."""
    if len(data) < 8:
        return []
    cid, csize = struct.unpack("<II", data[:8])
    if cid != CHUNKID_FILE_LIST:
        return []
    end = min(8 + (csize & 0x7FFFFFFF), len(data))
    names, pos = [], 8
    while pos + 2 <= end:
        mid, mlen = data[pos], data[pos + 1]
        raw = data[pos + 2:pos + 2 + mlen]
        pos += 2 + mlen
        if mid == 0x01:
            name = raw.split(b"\0")[0].decode("latin-1").strip()
            base = os.path.basename(name.replace("\\", "/"))
            if base and base.lower() != ".w3d":
                names.append(base)
    return names


def texture_costs(archives, tex_names, order, reduction):
    """Return (dds_bytes, tga_loaded_bytes, tga_as_dxt_bytes, missing_count)."""
    dds_total = tga_loaded = tga_dxt = missing = 0
    for name in tex_names:
        stem = os.path.splitext(name)[0]
        hit = archives.find(stem + ".dds", order)
        if hit:
            arc, off, _size = hit
            info = dds_info(archives.handle(arc), off)
            if info:
                est = engine_texture_bytes(*info, reduction)
                dds_total += est or 0
            continue
        hit = archives.find(stem + ".tga", order)
        if hit:
            arc, off, _size = hit
            info = tga_info(archives.handle(arc), off)
            if info:
                loaded, dxt = tga_memory(info[0], info[1], info[3], reduction)
                tga_loaded += loaded
                tga_dxt += dxt
            continue
        missing += 1
    return dds_total, tga_loaded, tga_dxt, missing


def main():
    global PRELIT_MODE
    ap = argparse.ArgumentParser(description="Per-map memory budget for Renegade.")
    ap.add_argument("data_dir", help="Renegade Data folder")
    ap.add_argument("--reduction", type=int, default=1,
                    help="texture reduction level to model (default 1)")
    ap.add_argument("--levels", default="*",
                    help='only these levels, e.g. "m*" for single player (default: all)')
    ap.add_argument("--top", type=int, default=0,
                    help="also list the N heaviest models in each level")
    ap.add_argument("--prelit", default="multitexture", choices=sorted(PRELIT_FALLBACK),
                    help="lighting mode to model (default multitexture, the game's default)")
    args = ap.parse_args()
    PRELIT_MODE = args.prelit

    archives = ArchiveSet(args.data_dir)
    if not archives.index:
        sys.exit(f"No archives found in {args.data_dir}")
    cache = {}
    base = archives.order_for()

    hit = archives.find("always.dep", base)
    if not hit:
        sys.exit("always.dep not found")
    always_names = parse_dep(archives.read(hit[0], hit[1], hit[2]))
    always = model_stats(archives, always_names, base, cache)
    a_dds, a_tga, a_tga_dxt, a_miss = texture_costs(
        archives, always["textures"], base, args.reduction)
    always_set = {n.lower() for n in always_names}

    print(f"Texture reduction {args.reduction}, lighting mode {args.prelit}.")
    print(f"\n=== Always loaded (always.dep from {hit[0]}) ===")
    print(f"  models listed {len(always_names)}, found {always['found']}, "
          f"missing {len(always['missing'])}")
    print(f"  meshes {always['meshes']}, vertices {always['verts']:,}, "
          f"triangles {always['tris']:,}")
    print(f"  model files on disk      {mb(always['w3d_bytes'])}")
    print(f"  anims/skeletons/other    {mb(always['anim_bytes'])}")
    print(f"  models, PC-style         {mb(always['pc'])}")
    print(f"  models, Xbox single copy {mb(always['single'])}")
    print(f"  models, Xbox lean        {mb(always['lean'])}")
    print(f"  textures: {len(always['textures'])} unique, {a_miss} not found")
    print(f"    DDS                    {mb(a_dds)}")
    print(f"    TGA as loaded today    {mb(a_tga)}   (as DXT: {mb(a_tga_dxt)})")

    a_tex = a_dds + a_tga_dxt
    rows = []
    for arc in archives.index:
        deps = [n for n in archives.index[arc] if n.endswith(".dep") and n != "always.dep"]
        for dep in deps:
            pattern = args.levels.lower()
            if not pattern.endswith(".dep"):
                pattern += ".dep"
            if not fnmatch.fnmatch(dep, pattern):
                continue
            off, size = archives.index[arc][dep]
            names = [n for n in parse_dep(archives.read(arc, off, size))
                     if n.lower() not in always_set]
            order = archives.order_for(arc)
            per_model = []
            lvl = model_stats(archives, names, order, cache, per_model)
            lvl["per_model"] = per_model
            extra_tex = lvl["textures"] - always["textures"]
            l_dds, _l_tga, l_tga_dxt, l_miss = texture_costs(
                archives, extra_tex, order, args.reduction)
            tex = a_tex + l_dds + l_tga_dxt
            totals = {k: always[k] + lvl[k] + tex for k in ("pc", "single", "lean")}
            rows.append((dep, lvl, len(extra_tex), tex, totals,
                         len(lvl["missing"]) + l_miss))

    print("\n=== Per level: totals = always-loaded + level models + all textures (TGAs as DXT) ===")
    print(f"  {'level':20s} {'models':>6s} {'verts':>9s} {'tris':>9s} {'textures':>9s} "
          f"{'PC-style':>9s} {'Xbox single':>12s} {'Xbox lean':>10s}")
    for dep, lvl, _ntex, tex, totals, _miss in sorted(rows, key=lambda r: -r[4]["pc"]):
        print(f"  {dep:20s} {lvl['found']:6d} {lvl['verts']:9,d} {lvl['tris']:9,d} "
              f"{mb(tex).strip():>9s} {mb(totals['pc']).strip():>9s} "
              f"{mb(totals['single']).strip():>12s} {mb(totals['lean']).strip():>10s}")

    if args.top:
        for dep, lvl, *_rest in sorted(rows, key=lambda r: -r[4]["pc"]):
            models = sorted(lvl["per_model"], key=lambda m: -m[1]["pc"])
            level_pc = sum(st["pc"] for _n, st in models) or 1
            print(f"\n=== {dep}: {args.top} heaviest of {len(models)} models ===")
            print(f"  {'model':32s} {'PC-style':>9s} {'share':>6s} {'Xbox lean':>10s} "
                  f"{'verts':>8s} {'tris':>8s} {'tex':>4s}")
            shown = 0
            for name, st in models[:args.top]:
                shown += st["pc"]
                print(f"  {name[:32]:32s} {mb(st['pc']).strip():>9s} "
                      f"{100 * st['pc'] / level_pc:5.1f}% {mb(st['lean']).strip():>10s} "
                      f"{st['verts']:8,d} {st['tris']:8,d} {len(st['textures']):4d}")
            print(f"  top {args.top} = {100 * shown / level_pc:.0f}% of this level's model memory")

    print("\n  Not included: kernel, game code, framebuffers, level data (.lsd/.ldd),")
    print("  sounds, or game state. Models loaded later by scripts are not followed.")


if __name__ == "__main__":
    main()
