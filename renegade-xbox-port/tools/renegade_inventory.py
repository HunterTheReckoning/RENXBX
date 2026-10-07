#!/usr/bin/env python3
"""
Renegade asset inventory for the Xbox port.

Reads the MIX1 archives (.mix, .dat, .dbs) in a Renegade Data folder and reports:
  - file counts and sizes by extension
  - textures that are NOT .dds (these take the slow Targa path)
  - estimated GPU texture memory per archive, computed the way
    ww3d2/ddsfile.cpp loads DDS files (reduction + dropping the 2 smallest mips)

Format follows wwlib/mixfile.cpp:
  header:  "MIX1", int32 header_offset, int32 names_offset
  at header_offset: int32 count, then count x (uint32 crc, uint32 offset, uint32 size)
  at names_offset:  int32 count, then count x (uint8 len incl. NUL, name bytes)
  Names are stored in the same (CRC-sorted) order as the file table.
  Offsets are absolute from the start of the archive.

Usage:
  python renegade_inventory.py "C:/Path/To/Renegade/Data" [--reduction N] [--csv out.csv]
  python renegade_inventory.py "C:/Path/To/Renegade/Data" --extract "*.dep" --out extracted

Version 2 adds: WAV format breakdown, TGA memory estimates (as loaded vs. as DXT),
and --extract to copy matching files out of the archives.
"""

import argparse
import csv
import fnmatch
import os
import struct
import sys
from collections import defaultdict

ARCHIVE_EXTS = (".mix", ".dat", ".dbs")
TEXTURE_EXTS = (".dds", ".tga")
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


WAV_FORMATS = {1: "PCM", 2: "MS ADPCM", 0x11: "IMA ADPCM", 0x55: "MP3", 3: "IEEE float"}


def wav_info(fh, offset, size):
    """Return (format_name, channels, rate, bits) from a WAV at offset."""
    fh.seek(offset)
    data = fh.read(min(size, 512))
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    pos = 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        (clen,) = struct.unpack("<I", data[pos + 4:pos + 8])
        if cid == b"fmt " and pos + 24 <= len(data):
            tag, ch, rate = struct.unpack("<HHI", data[pos + 8:pos + 16])
            (bits,) = struct.unpack("<H", data[pos + 22:pos + 24])
            return WAV_FORMATS.get(tag, f"tag 0x{tag:x}"), ch, rate, bits
        pos += 8 + clen + (clen & 1)
    return None


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


def main():
    ap = argparse.ArgumentParser(description="Inventory Renegade MIX archives.")
    ap.add_argument("data_dir", help="Renegade Data folder")
    ap.add_argument("--reduction", type=int, default=0,
                    help="texture reduction level to model (0 = full size)")
    ap.add_argument("--csv", help="write a per-file CSV here")
    ap.add_argument("--extract", help='copy files matching this pattern out, e.g. "*.dep"')
    ap.add_argument("--out", default="extracted", help="folder for --extract (default: extracted)")
    args = ap.parse_args()

    archives = sorted(f for f in os.listdir(args.data_dir)
                      if f.lower().endswith(ARCHIVE_EXTS))
    if not archives:
        sys.exit(f"No .mix/.dat/.dbs files found in {args.data_dir}")

    by_ext = defaultdict(lambda: [0, 0])
    non_dds_textures = []
    formats = defaultdict(int)
    rows = []
    per_archive = []
    wav_formats = defaultdict(lambda: [0, 0])
    tga_loaded_total = tga_dxt_total = 0
    tga_rows = []
    extracted = 0
    if args.extract:
        os.makedirs(args.out, exist_ok=True)

    for arc in archives:
        path = os.path.join(args.data_dir, arc)
        entries = read_mix(path)
        if entries is None:
            print(f"skipping {arc}: not a MIX1 archive")
            continue

        tex_bytes = 0
        with open(path, "rb") as fh:
            for name, off, size in entries:
                ext = os.path.splitext(name)[1].lower() or "<none>"
                by_ext[ext][0] += 1
                by_ext[ext][1] += size
                gpu = ""
                if args.extract and fnmatch.fnmatch(name.lower(), args.extract.lower()):
                    safe = name.replace("\\", "_").replace("/", "_")
                    dest = os.path.join(args.out, f"{os.path.splitext(arc)[0]}__{safe}")
                    fh.seek(off)
                    with open(dest, "wb") as outf:
                        outf.write(fh.read(size))
                    extracted += 1
                    fh.seek(off)
                if ext == ".wav":
                    wi = wav_info(fh, off, size)
                    key = f"{wi[0]} {wi[1]}ch {wi[2]}Hz {wi[3]}bit" if wi else "unreadable"
                    wav_formats[key][0] += 1
                    wav_formats[key][1] += size
                if ext == ".tga":
                    non_dds_textures.append((arc, name, size))
                    ti = tga_info(fh, off)
                    if ti:
                        loaded, dxt = tga_memory(ti[0], ti[1], ti[3], args.reduction)
                        tga_loaded_total += loaded
                        tga_dxt_total += dxt
                        tga_rows.append((loaded, arc, name, ti))
                        gpu = loaded
                elif ext == ".dds":
                    info = dds_info(fh, off)
                    if info:
                        w, h, mips, fourcc = info
                        formats[fourcc.decode("latin-1", "replace")] += 1
                        est = engine_texture_bytes(w, h, mips, fourcc, args.reduction)
                        if est is not None:
                            tex_bytes += est
                            gpu = est
                rows.append((arc, name, ext, size, gpu))
        per_archive.append((arc, len(entries), tex_bytes))

    print(f"\n=== File types (all archives) ===")
    for ext, (count, size) in sorted(by_ext.items(), key=lambda kv: -kv[1][1]):
        print(f"  {ext:10s} {count:6d} files  {mb(size)}")

    print(f"\n=== DDS formats ===")
    for fmt, count in sorted(formats.items(), key=lambda kv: -kv[1]):
        print(f"  {fmt:6s} {count:6d}")

    print(f"\n=== Estimated texture memory per archive (reduction {args.reduction}) ===")
    print("  Upper bound: assumes every texture in the archive is loaded.")
    for arc, count, tex in sorted(per_archive, key=lambda t: -t[2]):
        print(f"  {arc:30s} {count:6d} files  {mb(tex)}")

    print(f"\n=== WAV formats ===")
    for key, (count, size) in sorted(wav_formats.items(), key=lambda kv: -kv[1][1]):
        print(f"  {key:32s} {count:6d} files  {mb(size)}")

    print(f"\n=== TGA memory (reduction {args.reduction}, all TGAs, full mip chain) ===")
    print(f"  as loaded today (32-bit): {mb(tga_loaded_total)}")
    print(f"  if converted to DXT:      {mb(tga_dxt_total)}")
    print(f"  largest TGAs as loaded:")
    for loaded, arc, name, (w, h, bpp, alpha) in sorted(tga_rows, reverse=True)[:15]:
        print(f"    {mb(loaded)}  {w}x{h} {bpp}bit{' alpha' if alpha else ''}  {arc}: {name}")

    if args.extract:
        print(f"\nExtracted {extracted} file(s) matching {args.extract} to {args.out}")

    print(f"\n=== Non-DDS textures ({len(non_dds_textures)}) ===")
    for arc, name, size in non_dds_textures[:50]:
        print(f"  {arc:20s} {name}")
    if len(non_dds_textures) > 50:
        print(f"  ... and {len(non_dds_textures) - 50} more (use --csv for the full list)")

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["archive", "file", "ext", "size_bytes", "est_texture_bytes"])
            w.writerows(rows)
        print(f"\nWrote {args.csv}")


if __name__ == "__main__":
    main()
