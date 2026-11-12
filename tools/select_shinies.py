#!/usr/bin/env python3
"""Copy ranked shiny sprites into a LittleFS staging dir until the margin is hit.

Usage: select_shinies.py SRC STAGING LIST IMG_SIZE MARGIN BLOCK

SRC holds pNNN.bin / psNNN.bin. STAGING/mons already has the normal sprites.
LIST is tools/shiny_175c.txt (dex number, most popular first). IMG_SIZE is the
storage partition in bytes. MARGIN bytes of that partition are left unused.
BLOCK is the LittleFS block size used to estimate metadata.

A shiny that does not fit is skipped; later smaller ones may still fit.
The firmware falls back to the normal sprite when psNNN.bin is absent.
"""
import os
import shutil
import sys


def budget(paths, block):
    used = block  # superblock
    for path in paths:
        size = os.path.getsize(path)
        used += (size + block - 1) // block * block
        used += block  # one metadata block per file
    return used


def main():
    if len(sys.argv) != 7:
        print("usage: select_shinies.py SRC STAGING LIST IMG MARGIN BLOCK", file=sys.stderr)
        return 2
    src, staging, list_path, img_s, margin_s, block_s = sys.argv[1:]
    img, margin, block = int(img_s), int(margin_s), int(block_s)
    mons = os.path.join(staging, "mons")
    have = [os.path.join(mons, name) for name in os.listdir(mons) if os.path.isfile(os.path.join(mons, name))]
    limit = img - margin
    included = []
    skipped = []
    seen = set()
    with open(list_path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            dex = int(line.split()[0])
            if dex in seen or not 1 <= dex <= 151:
                print(f"bad shiny entry: {raw.rstrip()}", file=sys.stderr)
                return 1
            seen.add(dex)
            name = f"ps{dex:03d}.bin"
            path = os.path.join(src, name)
            if not os.path.isfile(path):
                print(f"missing {name}", file=sys.stderr)
                return 1
            if budget(have + [path], block) > limit:
                skipped.append(dex)
                continue
            shutil.copy2(path, os.path.join(mons, name))
            have.append(os.path.join(mons, name))
            included.append(dex)
    raw_bytes = sum(os.path.getsize(p) for p in have)
    print(f"shinies {len(included)} included, {len(skipped)} omitted")
    print("included:", " ".join(str(n) for n in included))
    if skipped:
        print("omitted:", " ".join(str(n) for n in skipped))
    print(f"raw {raw_bytes} bytes; estimate {budget(have, block)} / {img} (margin {margin})")
    if raw_bytes >= img:
        print("raw pack is larger than the partition", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
