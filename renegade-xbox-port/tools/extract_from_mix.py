#!/usr/bin/env python3
"""List or extract files from a Renegade archive (.mix / .dat), as loose files.

  python3 extract_from_mix.py ARCHIVE --list [TEXT]       names containing TEXT (any case)
  python3 extract_from_mix.py ARCHIVE OUTDIR NAME [NAME...] extract those files into OUTDIR

Names match without regard to case, as the engine does. For .dds files the format, size and
mipmap count are printed from the file's header. The engine finds loose files by name in the
game's folder, so files placed in a test's data folder (which becomes D:\\ on the disc) are
loaded exactly as if they came from the archive.

Game files are EA's: keep them out of your git repository."""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from renegade_inventory import read_mix


def dds_info(data):
    """'DXT1 256x256, 9 levels' style summary of a DDS header, or None."""
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    height, width = struct.unpack("<II", data[12:20])
    mips = struct.unpack("<I", data[28:32])[0] or 1
    pf_flags, fourcc, bits = struct.unpack("<I4sI", data[80:92])
    if pf_flags & 0x4:
        fmt = fourcc.decode("latin-1")
    else:
        alpha = struct.unpack("<I", data[104:108])[0]
        fmt = f"{bits}-bit {'with' if alpha else 'no'} alpha"
    return f"{fmt} {width}x{height}, {mips} level{'s' if mips != 1 else ''}"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    archive = sys.argv[1]
    entries = read_mix(archive)
    if entries is None:
        print(f"{archive}: not a Renegade MIX archive")
        return 1
    if sys.argv[2] == "--list":
        text = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        shown = [e for e in entries if text in e[0].lower()]
        for name, _off, size in sorted(shown, key=lambda e: e[0].lower()):
            print(f"{size:>10}  {name}")
        print(f"{len(shown)} of {len(entries)} files")
        return 0
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    by_name = {e[0].lower(): e for e in entries}
    missing = 0
    with open(archive, "rb") as f:
        for wanted in sys.argv[3:]:
            entry = by_name.get(wanted.lower())
            if not entry:
                print(f"  not found: {wanted}")
                missing += 1
                continue
            name, off, size = entry
            f.seek(off)
            data = f.read(size)
            out_name = os.path.basename(name.replace("\\", "/"))
            with open(os.path.join(outdir, out_name), "wb") as out:
                out.write(data)
            info = dds_info(data) if out_name.lower().endswith(".dds") else None
            print(f"  {out_name}: {size} bytes" + (f", {info}" if info else ""))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
