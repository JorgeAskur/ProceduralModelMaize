from __future__ import annotations

import math
import shutil
from pathlib import Path

# Field layout configuration (counts + spacing)
# Grid size (columns x rows). This controls plant count layout.
GRID_COLS = 100
GRID_ROWS = 20
# Fixed spacing between plant centers.
SPACING_X = 0.300
SPACING_Z = 0.750
YAW_DEGREES = 45.0

# If True, the grid is centered around (0, 0).
# If False, the grid starts at (0, 0).
CENTER_FIELD_ON_ORIGIN = False

# Directory containing the plant .obj files.
PLANTS_DIR = Path(__file__).resolve().parent / "plants"
# Output directory for transformed .obj files (non-destructive).
OUTPUT_DIR = PLANTS_DIR / "field_100x20"

ASSET_EXTENSIONS = {
    ".mtl",
    ".png",
    ".jpg",
    ".jpeg",
    ".tga",
    ".bmp",
    ".gif",
    ".dds",
}


def format_float(value: float) -> str:
    return f"{value:.9f}"


def transform_obj(
    src_path: Path,
    dst_path: Path,
    translate_x: float,
    translate_z: float,
    cos_yaw: float,
    sin_yaw: float,
) -> None:
    with src_path.open("r", newline="") as src, dst_path.open("w", newline="") as dst:
        for line in src:
            if line.startswith("v "):
                parts = line.strip().split()
                if len(parts) >= 4:
                    x = float(parts[1])
                    y = float(parts[2])
                    z = float(parts[3])

                    # Rotate around Y (yaw), then translate in X/Z.
                    x_rot = (x * cos_yaw) - (z * sin_yaw)
                    z_rot = (x * sin_yaw) + (z * cos_yaw)
                    x_new = x_rot + translate_x
                    z_new = z_rot + translate_z

                    new_parts = ["v", format_float(x_new), format_float(y), format_float(z_new)]
                    if len(parts) > 4:
                        new_parts.extend(parts[4:])
                    dst.write(" ".join(new_parts) + "\n")
                    continue

            dst.write(line)


def obj_sort_key(path: Path) -> tuple[int, int, str]:
    stem = path.stem
    suffix = stem.split("_")[-1]
    if suffix.isdigit():
        return (0, int(suffix), stem)
    return (1, 0, stem)


def copy_assets(src_dir: Path, dst_dir: Path) -> None:
    dst_dir.mkdir(parents=True, exist_ok=True)
    for path in src_dir.iterdir():
        if path.is_file() and path.suffix.lower() in ASSET_EXTENSIONS:
            shutil.copy2(path, dst_dir / path.name)


def main() -> int:
    if not PLANTS_DIR.exists():
        raise FileNotFoundError(f"Plants directory not found: {PLANTS_DIR}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    copy_assets(PLANTS_DIR, OUTPUT_DIR)

    obj_files = sorted(
        (p for p in PLANTS_DIR.glob("*.obj") if p.is_file() and p.stat().st_size > 0),
        key=obj_sort_key,
    )

    if not obj_files:
        print("No .obj files found to process.")
        return 0

    cols = int(GRID_COLS)
    rows = int(GRID_ROWS)
    if cols <= 0 or rows <= 0:
        raise ValueError("GRID_COLS and GRID_ROWS must be > 0.")
    if SPACING_X <= 0 or SPACING_Z <= 0:
        raise ValueError("SPACING_X and SPACING_Z must be > 0.")

    capacity = cols * rows
    if len(obj_files) > capacity:
        print(
            f"Warning: {len(obj_files)} plants found but grid capacity is {capacity}. "
            f"Only the first {capacity} plants will be placed."
        )
        obj_files = obj_files[:capacity]

    x_spacing = SPACING_X
    z_spacing = SPACING_Z
    grid_width = cols * x_spacing
    grid_depth = rows * z_spacing

    if CENTER_FIELD_ON_ORIGIN:
        x_offset = -grid_width / 2
        z_offset = -grid_depth / 2
    else:
        x_offset = 0.0
        z_offset = 0.0

    yaw_radians = math.radians(YAW_DEGREES)
    cos_yaw = math.cos(yaw_radians)
    sin_yaw = math.sin(yaw_radians)

    for index, obj_path in enumerate(obj_files):
        row = index // cols
        col = index % cols

        translate_x = x_offset + (col + 0.5) * x_spacing
        translate_z = z_offset + (row + 0.5) * z_spacing

        dst_path = OUTPUT_DIR / obj_path.name
        transform_obj(obj_path, dst_path, translate_x, translate_z, cos_yaw, sin_yaw)

    print(
        f"Placed {len(obj_files)} plants in a {cols}x{rows} grid "
        f"with {YAW_DEGREES} degrees yaw."
    )
    print(f"Spacing: {x_spacing:.3f} (x) x {z_spacing:.3f} (z)")
    print(f"Output written to: {OUTPUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
