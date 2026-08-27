#!/usr/bin/env python3
"""Build a deterministic Britain technical map fixture for Core World Compiler.

Coast/land comes from Basemap's bundled GSHHS-derived land/sea mask. Province
regions are deliberately synthetic Voronoi cells and MUST NOT be treated as
1836 historical borders. This fixture exists to validate the paging, coast SDF,
compression and runtime streaming pipeline without shipping a large GIS source.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from mpl_toolkits.basemap import maskoceans
from scipy import ndimage
from scipy.spatial import cKDTree

PAGE = 128
WIDTH = 2048
HEIGHT = 2048
LON_MIN, LON_MAX = -11.0, 3.0
LAT_MIN, LAT_MAX = 49.0, 60.5
PROVINCES = 72
SEED = 1836
EARTH_RADIUS_M = 6_378_137.0


def mercator_xy(lon_deg: np.ndarray, lat_deg: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    lon = np.deg2rad(lon_deg)
    lat = np.deg2rad(np.clip(lat_deg, -80.0, 80.0))
    x = EARTH_RADIUS_M * lon
    y = EARTH_RADIUS_M * np.log(np.tan(np.pi * 0.25 + lat * 0.5))
    return x, y


def build_land_mask() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    lons = np.linspace(LON_MIN, LON_MAX, WIDTH, dtype=np.float64)
    lats = np.linspace(LAT_MIN, LAT_MAX, HEIGHT, dtype=np.float64)
    lon_grid, lat_grid = np.meshgrid(lons, lats)
    probe = np.zeros((HEIGHT, WIDTH), dtype=np.float32)
    ocean_masked = maskoceans(lon_grid, lat_grid, probe, inlands=True, resolution="i", grid=2.5)
    # maskoceans masks ocean/lakes; unmasked cells are land.
    land = ~np.ma.getmaskarray(ocean_masked)
    return land, lon_grid, lat_grid


def synthetic_provinces(land: np.ndarray, lon_grid: np.ndarray, lat_grid: np.ndarray) -> np.ndarray:
    rng = np.random.default_rng(SEED)
    ys, xs = np.nonzero(land)
    if len(xs) < PROVINCES:
        raise RuntimeError("technical fixture has too little land")

    # Farthest-point-ish initialization from a random candidate pool gives a more
    # even synthetic partition than fully random centers while staying deterministic.
    candidate_count = min(len(xs), 20_000)
    chosen = rng.choice(len(xs), size=candidate_count, replace=False)
    candidates = np.column_stack((xs[chosen], ys[chosen])).astype(np.float64)
    centers = [candidates[rng.integers(0, len(candidates))]]
    min_d2 = np.sum((candidates - centers[0]) ** 2, axis=1)
    for _ in range(1, PROVINCES):
        idx = int(np.argmax(min_d2))
        c = candidates[idx]
        centers.append(c)
        min_d2 = np.minimum(min_d2, np.sum((candidates - c) ** 2, axis=1))
    tree = cKDTree(np.asarray(centers))

    ids = np.zeros((HEIGHT, WIDTH), dtype=np.uint16)
    # Query in strips to avoid a multi-million-point temporary allocation.
    xx = np.arange(WIDTH, dtype=np.float64)
    for y0 in range(0, HEIGHT, 128):
        y1 = min(HEIGHT, y0 + 128)
        gx, gy = np.meshgrid(xx, np.arange(y0, y1, dtype=np.float64))
        points = np.column_stack((gx.ravel(), gy.ravel()))
        _, nearest = tree.query(points, workers=-1)
        strip = (nearest.reshape(y1 - y0, WIDTH) + 1).astype(np.uint16)
        strip[~land[y0:y1]] = 0
        ids[y0:y1] = strip
    return ids


def coast_sdf_quantized(land: np.ndarray, lon_grid: np.ndarray, lat_grid: np.ndarray) -> np.ndarray:
    # Approximate source pixel spacing at the fixture center in Web Mercator.
    x, y = mercator_xy(lon_grid, lat_grid)
    px = float(np.median(np.diff(x[HEIGHT // 2])))
    py = float(np.median(np.diff(y[:, WIDTH // 2])))
    pixel_m = (abs(px) + abs(py)) * 0.5
    inside = ndimage.distance_transform_edt(land) * pixel_m
    outside = ndimage.distance_transform_edt(~land) * pixel_m
    sdf_m = inside - outside
    q = np.rint(sdf_m / 0.5)
    return np.clip(q, -32767, 32767).astype("<i2")


def write_fixture(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    pages = out / "pages"
    pages.mkdir(exist_ok=True)
    land, lon_grid, lat_grid = build_land_mask()
    ids = synthetic_provinces(land, lon_grid, lat_grid)
    coast_q = coast_sdf_quantized(land, lon_grid, lat_grid)

    manifest_lines = ["# type level x y variant file"]
    for py in range(HEIGHT // PAGE):
        for px in range(WIDTH // PAGE):
            y0, y1 = py * PAGE, (py + 1) * PAGE
            x0, x1 = px * PAGE, (px + 1) * PAGE
            # ProvinceRasterPage convention is uint16 with 0=water, id+1 on land.
            province_page = ids[y0:y1, x0:x1].astype("<u2", copy=False)
            coast_page = coast_q[y0:y1, x0:x1]
            path = pages / f"province_coast_l0_{px}_{py}.bin"
            with path.open("wb") as f:
                f.write(province_page.tobytes(order="C"))
                f.write(coast_page.tobytes(order="C"))
            manifest_lines.append(f"province_coast 0 {px} {py} 0 pages/{path.name}")

    metadata = {
        "fixture": "britain_technical",
        "historical_accuracy": False,
        "province_model": "synthetic_voronoi",
        "province_count": int(ids.max()),
        "raster": [WIDTH, HEIGHT],
        "page": [PAGE, PAGE],
        "bounds_lon_lat": [LON_MIN, LAT_MIN, LON_MAX, LAT_MAX],
        "coast_source": "Basemap bundled GSHHS-derived land/sea mask",
        "purpose": "Core paging/coast-SDF/world-pack technical validation only",
    }
    metadata_path = out / "metadata.json"
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    manifest_lines.insert(1, "metadata 0 0 0 0 metadata.json")
    (out / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

    # Small preview arrays are useful for validation without requiring the engine.
    np.save(out / "province_ids.npy", ids)
    np.save(out / "coast_sdf_q.npy", coast_q)
    print(json.dumps(metadata, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=Path("demo/britain_technical_input"))
    args = parser.parse_args()
    write_fixture(args.out)


if __name__ == "__main__":
    main()
