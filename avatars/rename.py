#!/usr/bin/env python3
import re
import csv
import json
import random
from pathlib import Path

# ================= CONFIG =================
DEFAULT_DIR = "."
DRY_RUN = False
KEEP_EXTENSION = True

# ================= SCI-FI NAME POOLS =================
PREFIX = [
    "Astra", "Nova", "Vanta", "Orion", "Lyra", "Cyra", "Kairo", "Zara", "Nyx",
    "Helio", "Talon", "Sable", "Eon", "Vex", "Arden", "Riven", "Mira", "Sol",
    "Dax", "Kestrel", "Juno", "Axiom", "Vega", "Drift", "Quanta", "Zephyr"
]

CORE = [
    "Specter", "Cipher", "Strider", "Warden", "Nomad", "Sentinel", "Vanguard",
    "Echo", "Pulse", "Shard", "Raptor", "Vector", "Catalyst", "Mirage",
    "Harbinger", "Lancer", "Rook", "Falcon", "Nexus", "Circuit", "Phantom"
]

SUFFIX = [
    "IX", "VII", "Prime", "MkII", "MkIII", "Sigma", "Kappa", "Delta",
    "Zero", "One", "Nine", "Void", "Drake", "Rune", "Core", "Arc"
]

def make_name(rng: random.Random) -> str:
    return f"{rng.choice(PREFIX)}{rng.choice(CORE)}{rng.choice(SUFFIX)}"

# ================= HELPERS =================
def normalize_key(stem: str) -> str:
    s = re.sub(r"^Default_", "", stem)
    s = re.sub(
        r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}",
        "",
        s,
        flags=re.I,
    )
    s = re.sub(r"_[0-9]+$", "", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s.lower()

def safe_filename(name: str) -> str:
    # ASCII-only, Windows & web safe
    return re.sub(r"[^A-Za-z0-9._-]+", "", name)

# ================= MAIN =================
def main(dirpath: str):
    p = Path(dirpath)
    if not p.is_dir():
        raise SystemExit(f"Directory not found: {p}")

    files = [
        f for f in p.iterdir()
        if f.is_file()
        and f.name.startswith("Default_")
        and f.suffix.lower() in [".png", ".jpg", ".jpeg", ".webp"]
    ]

    if not files:
        print("No Default_* image files found.")
        return

    rng = random.Random(1337)

    decorated = []
    for f in files:
        decorated.append((normalize_key(f.stem), f.name.lower(), f))
    decorated.sort(key=lambda x: (x[0], x[1]))

    used_names = set()
    mapping_rows = []
    json_map = []

    for _, _, f in decorated:
        name = make_name(rng)
        while name in used_names:
            name = make_name(rng)
        used_names.add(name)

        ext = f.suffix if KEEP_EXTENSION else ".png"
        new_name = safe_filename(name) + ext
        new_path = f.with_name(new_name)

        if new_path.exists() and new_path.resolve() != f.resolve():
            raise SystemExit(f"Collision detected: {new_name}")

        mapping_rows.append([f.name, new_name])
        json_map.append({"old": f.name, "new": new_name})

    print("")
    print(f"Renaming {len(mapping_rows)} avatar files:")
    print("")

    for old, new in mapping_rows[:20]:
        print(f"{old} -> {new}")
    if len(mapping_rows) > 20:
        print(f"... and {len(mapping_rows) - 20} more")

    if not DRY_RUN:
        csv_path = p / "avatar_rename_map.csv"
        json_path = p / "avatar_rename_map.json"

        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["old_name", "new_name"])
            w.writerows(mapping_rows)

        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(json_map, f, indent=2)

        for old, new in mapping_rows:
            (p / old).rename(p / new)

        print("")
        print("Done.")
        print("Generated:")
        print(" - avatar_rename_map.csv")
        print(" - avatar_rename_map.json")
    else:
        print("")
        print("DRY RUN - no files renamed.")

# ================= ENTRY =================
if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Rename Leonardo avatar images to sci-fi codenames")
    ap.add_argument("--dir", default=DEFAULT_DIR, help="Avatar directory")
    ap.add_argument("--dry", action="store_true", help="Dry run only")
    args = ap.parse_args()

    DRY_RUN = args.dry
    main(args.dir)
