#!/usr/bin/env python3
import json
import argparse
from pathlib import Path

def rel_url(base_url: str, rel_path: str) -> str:
    return base_url.rstrip("/") + "/" + rel_path.replace("\\", "/").lstrip("/")

def scan_dir(dir_path: Path):
    return sorted([p for p in dir_path.glob("*.png") if p.is_file()], key=lambda p: p.name.lower())

def main(root: Path, base_url: str, out_file: Path):
    standard_dir = root / "standard"
    premium_dir  = root / "premium"

    if not standard_dir.is_dir() and not premium_dir.is_dir():
        raise SystemExit(
            f"Expected {standard_dir} and/or {premium_dir} to exist.\n"
            f"Create folders or use Option B (pattern-based)."
        )

    items = []

    if standard_dir.is_dir():
        for p in scan_dir(standard_dir):
            key = f"standard/{p.name}"
            items.append({
                "key": key,
                "tier": "standard",
                "url": rel_url(base_url, key),
            })

    if premium_dir.is_dir():
        for p in scan_dir(premium_dir):
            key = f"premium/{p.name}"
            items.append({
                "key": key,
                "tier": "premium",
                "url": rel_url(base_url, key),
            })

    doc = {
        "version": 1,
        "baseUrl": base_url.rstrip("/"),
        "count": len(items),
        "avatars": items
    }

    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(doc, indent=2), encoding="utf-8")
    print(f"Wrote {out_file} with {len(items)} avatars.")

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Generate avatars.json for avatar picker (standard/premium folders).")
    ap.add_argument("--root", required=True, help="Root folder containing standard/ and/or premium/")
    ap.add_argument("--base-url", required=True, help="Public base URL, e.g. https://gameinfo.daraempire.com/avatars")
    ap.add_argument("--out", default="avatars.json", help="Output JSON file path")
    args = ap.parse_args()

    main(Path(args.root), args.base_url, Path(args.out))
