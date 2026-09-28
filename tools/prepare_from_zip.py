#!/usr/bin/env python3
"""Prepare a ScummVM-friendly Zero Comico data directory from zero.zip.

The complete archive used during development stores installed files under
"program files/Medusa Games/Zero Comico/" while CD movies are under "Data/".
This tool flattens the installed prefix and places Data alongside it.
"""

from pathlib import Path
import argparse
import shutil
import zipfile

INSTALL_PREFIX = "program files/Medusa Games/Zero Comico/"


def safe_rel(name: str):
    p = Path(name)
    if p.is_absolute() or ".." in p.parts:
        return None
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip", type=Path)
    ap.add_argument("output", type=Path)
    ns = ap.parse_args()
    ns.output.mkdir(parents=True, exist_ok=True)

    copied = 0
    with zipfile.ZipFile(ns.zip) as zf:
        for info in zf.infolist():
            name = info.filename.replace("\\", "/")
            rel = None
            if name.startswith(INSTALL_PREFIX):
                rel = name[len(INSTALL_PREFIX):]
            elif name.startswith("Data/"):
                rel = name
            if not rel or info.is_dir():
                continue
            rel_path = safe_rel(rel)
            if rel_path is None:
                raise RuntimeError(f"Unsafe archive member: {name}")
            dest = ns.output / rel_path
            dest.parent.mkdir(parents=True, exist_ok=True)
            with zf.open(info) as src, dest.open("wb") as dst:
                shutil.copyfileobj(src, dst)
            copied += 1

    print(f"Prepared {copied} files in {ns.output}")
    print("Point ScummVM at that directory.")


if __name__ == "__main__":
    main()
