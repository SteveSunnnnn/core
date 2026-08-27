#!/usr/bin/env python3
"""Compile province GIS into Core WorldPack source chunks.

Required province fields:
  province_key, state_key, country_tag, market_key
Optional fields:
  state_capital (bool/int), population, gdp, treasury, tax_rate

Input may be GeoJSON, GeoPackage, Shapefile, or anything GeoPandas can read.
All runtime geometry is baked offline. Output is a manifest directory consumed by
`core_world_compiler`.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

import geopandas as gpd
import numpy as np
import rasterio
from rasterio.features import rasterize
from rasterio.transform import from_origin
from rasterio.vrt import WarpedVRT
from rasterio.enums import Resampling
from scipy import ndimage
from shapely.geometry import box

PAGE = 128
CHUNK_M = 64_000.0
DEFAULT_RESOLUTION_M = CHUNK_M / PAGE
MAX_COAST_M = 16_000.0
PLC_MAGIC = 0x31434C50
ANC_MAGIC = 0x31434E41

PLACEMENT_BUILDABLE = 0
PLACEMENT_URBAN = 1
PLACEMENT_RURAL = 2
PLACEMENT_INDUSTRIAL = 3
PLACEMENT_FARM = 4
PLACEMENT_MINE = 5
PLACEMENT_PORT = 6
PLACEMENT_VEGETATION = 7
FLAG_BUILDABLE = 1
FLAG_COASTAL = 2


def _u16_string(text: str) -> bytes:
    b = text.encode("utf-8")
    if len(b) > 65535:
        raise ValueError(f"key too long: {text[:80]}")
    return struct.pack("<H", len(b)) + b


def _write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _stable_rows(provinces: gpd.GeoDataFrame):
    required = ["province_key", "state_key", "country_tag", "market_key"]
    missing = [c for c in required if c not in provinces.columns]
    if missing:
        raise ValueError(f"province layer missing fields: {', '.join(missing)}")
    for c in required:
        if provinces[c].isna().any():
            raise ValueError(f"province layer contains null {c}")
        provinces[c] = provinces[c].astype(str)
    if provinces["province_key"].duplicated().any():
        dup = provinces.loc[provinces["province_key"].duplicated(), "province_key"].iloc[0]
        raise ValueError(f"duplicate province_key: {dup}")
    return provinces.sort_values("province_key", kind="stable").reset_index(drop=True)


def _repair_project(path: Path, layer: str | None = None) -> gpd.GeoDataFrame:
    gdf = gpd.read_file(path, layer=layer)
    if gdf.empty:
        raise ValueError("province layer is empty")
    if gdf.crs is None:
        raise ValueError("province layer has no CRS")
    gdf = gdf[gdf.geometry.notna() & ~gdf.geometry.is_empty].copy()
    gdf.geometry = gdf.geometry.make_valid()
    gdf = gdf[gdf.geometry.geom_type.isin(["Polygon", "MultiPolygon"])].copy()
    if gdf.empty:
        raise ValueError("province layer has no polygon geometry after repair")
    gdf = gdf.to_crs(3857)
    return _stable_rows(gdf)


def _ids(gdf: gpd.GeoDataFrame):
    countries = sorted(gdf.country_tag.unique().tolist())
    country_id = {k: i for i, k in enumerate(countries)}
    markets = sorted(gdf.market_key.unique().tolist())
    market_id = {k: i for i, k in enumerate(markets)}
    market_owner: dict[str, str] = {}
    for key, group in gdf.groupby("market_key", sort=False):
        owners = sorted(group.country_tag.unique().tolist())
        if len(owners) != 1:
            raise ValueError(f"market {key} has multiple owners in bootstrap data: {owners}")
        market_owner[str(key)] = owners[0]
    states = sorted(gdf.state_key.unique().tolist())
    state_id = {k: i for i, k in enumerate(states)}
    state_meta = {}
    for key, group in gdf.groupby("state_key", sort=False):
        owners = sorted(group.country_tag.unique().tolist())
        markets_here = sorted(group.market_key.unique().tolist())
        if len(owners) != 1 or len(markets_here) != 1:
            raise ValueError(f"state {key} must have one bootstrap owner and market")
        capital = None
        if "state_capital" in group.columns:
            marked = group[group.state_capital.fillna(False).astype(bool)]
            if len(marked) > 1:
                raise ValueError(f"state {key} has multiple state_capital rows")
            if len(marked) == 1:
                capital = str(marked.iloc[0].province_key)
        if capital is None:
            capital = sorted(group.province_key.astype(str).tolist())[0]
        state_meta[str(key)] = (owners[0], markets_here[0], capital)
    province_id = {str(k): i for i, k in enumerate(gdf.province_key.tolist())}
    return countries, country_id, markets, market_id, market_owner, states, state_id, state_meta, province_id


def _country_defaults(gdf: gpd.GeoDataFrame, country: str):
    group = gdf[gdf.country_tag == country]
    def first_or(name: str, default: float):
        if name not in group.columns:
            return default
        vals = group[name].dropna()
        return float(vals.iloc[0]) if not vals.empty else default
    return (
        first_or("population", 0.0),
        first_or("gdp", 0.0),
        first_or("treasury", 0.0),
        first_or("tax_rate", 0.2),
    )


def _write_definitions(out: Path, gdf: gpd.GeoDataFrame, ids):
    countries, country_id, markets, market_id, market_owner, states, state_id, state_meta, province_id = ids
    b = bytearray(struct.pack("<I", len(countries)))
    for c in countries:
        pop, gdp, treasury, tax = _country_defaults(gdf, c)
        b += _u16_string(c) + struct.pack("<dddd", pop, gdp, treasury, tax)
    _write(out / "definitions/countries.bin", b)

    b = bytearray(struct.pack("<I", len(markets)))
    for m in markets:
        b += struct.pack("<I", country_id[market_owner[m]])
    _write(out / "definitions/markets.bin", b)

    b = bytearray(struct.pack("<I", len(states)))
    for s in states:
        owner, market, capital = state_meta[s]
        b += _u16_string(s)
        b += struct.pack("<III", country_id[owner], market_id[market], province_id[capital])
    _write(out / "definitions/states.bin", b)

    reps = gdf.geometry.representative_point()
    b = bytearray(struct.pack("<I", len(gdf)))
    for i, row in gdf.iterrows():
        area = max(1, int(round(row.geometry.area / 1_000_000.0)))
        b += _u16_string(str(row.province_key))
        b += struct.pack("<III", state_id[str(row.state_key)], country_id[str(row.country_tag)], market_id[str(row.market_key)])
        b += struct.pack("<ddI", float(reps.iloc[i].x), float(reps.iloc[i].y), min(area, 0xFFFFFFFF))
    _write(out / "definitions/provinces.bin", b)
    return reps


def _adjacency(out: Path, gdf: gpd.GeoDataFrame):
    n = len(gdf)
    neighbors: list[list[int]] = [[] for _ in range(n)]
    sindex = gdf.sindex
    for i, geom in enumerate(gdf.geometry):
        for j in sindex.query(geom, predicate="touches"):
            j = int(j)
            if j <= i:
                continue
            shared = geom.boundary.intersection(gdf.geometry.iloc[j].boundary)
            if shared.length <= 1.0:
                continue
            neighbors[i].append(j)
            neighbors[j].append(i)
    offsets = [0]
    flat = bytearray()
    for row in neighbors:
        row.sort()
        for j in row:
            flat += struct.pack("<IHH", j, 1, 256)  # Land, base movement cost 1.0 (Q8)
        offsets.append(offsets[-1] + len(row))
    _write(out / "definitions/adjacency_offsets.bin", struct.pack("<I", len(offsets)) + struct.pack(f"<{len(offsets)}I", *offsets))
    _write(out / "definitions/adjacency_neighbors.bin", struct.pack("<I", offsets[-1]) + flat)
    return offsets[-1] // 2


def _chunk_range(bounds):
    minx, miny, maxx, maxy = bounds
    return (
        math.floor(minx / CHUNK_M), math.floor(maxx / CHUNK_M),
        math.floor(miny / CHUNK_M), math.floor(maxy / CHUNK_M),
    )


def _placement_payload(records):
    b = bytearray(struct.pack("<II", PLC_MAGIC, len(records)))
    for province, x, y, z, cls, flags, weight in records:
        b += struct.pack("<IHHhBBHH", province, x, y, z, cls, flags, weight, 0)
    return bytes(b)


def _anchor_payload(records):
    b = bytearray(struct.pack("<II", ANC_MAGIC, len(records)))
    for province, x, y, z, importance, key_hash in records:
        b += struct.pack("<IHHhHQ", province, x, y, z, importance, key_hash)
    return bytes(b)


def _fnv1a64(text: str) -> int:
    h = 0xCBF29CE484222325
    for x in text.encode("utf-8"):
        h ^= x
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def _compile_pages(out: Path, gdf: gpd.GeoDataFrame, province_id, reps, resolution: float, dem: Path | None):
    if abs(resolution - DEFAULT_RESOLUTION_M) > 1e-6:
        raise ValueError("Core 1.0 ProvinceCoast/Placement wire currently requires 500 m pixels (128 px = 64 km)")
    manifest = []
    min_cx, max_cx, min_cy, max_cy = _chunk_range(gdf.total_bounds)
    sindex = gdf.sindex
    coast_margin_px = int(math.ceil(MAX_COAST_M / resolution)) + 2
    default_anchors: dict[tuple[int, int], list[tuple]] = {}
    for i, row in gdf.iterrows():
        p = province_id[str(row.province_key)]
        x, y = float(reps.iloc[i].x), float(reps.iloc[i].y)
        cx, cy = math.floor(x / CHUNK_M), math.floor(y / CHUNK_M)
        lx, ly = int(round(x - cx * CHUNK_M)), int(round(y - cy * CHUNK_M))
        lx, ly = min(max(lx, 0), 63999), min(max(ly, 0), 63999)
        default_anchors.setdefault((cx, cy), []).append((p, lx, ly, 0, 1, _fnv1a64(str(row.province_key))))

    dem_src = rasterio.open(dem) if dem else None
    dem_vrt = WarpedVRT(dem_src, crs="EPSG:3857", resampling=Resampling.bilinear) if dem_src else None
    page_count = candidate_count = 0
    try:
        for cy in range(min_cy, max_cy + 1):
            for cx in range(min_cx, max_cx + 1):
                x0, y0 = cx * CHUNK_M, cy * CHUNK_M
                x1, y1 = x0 + CHUNK_M, y0 + CHUNK_M
                hits = list(map(int, sindex.query(box(x0, y0, x1, y1), predicate="intersects")))
                if not hits:
                    continue
                shapes = [(gdf.geometry.iloc[i], province_id[str(gdf.iloc[i].province_key)] + 1) for i in hits]
                transform = from_origin(x0, y1, resolution, resolution)
                province_page = rasterize(shapes, out_shape=(PAGE, PAGE), transform=transform, fill=0, dtype="uint16", all_touched=False)
                if not np.any(province_page):
                    continue

                # Coast SDF uses an expanded raster so a page boundary does not become a fake coastline.
                m = coast_margin_px
                ex0, ey0 = x0 - m * resolution, y0 - m * resolution
                ex1, ey1 = x1 + m * resolution, y1 + m * resolution
                ehits = list(map(int, sindex.query(box(ex0, ey0, ex1, ey1), predicate="intersects")))
                etransform = from_origin(ex0, ey1, resolution, resolution)
                epage = rasterize([(gdf.geometry.iloc[i], 1) for i in ehits], out_shape=(PAGE + 2*m, PAGE + 2*m), transform=etransform, fill=0, dtype="uint8", all_touched=False)
                land = epage.astype(bool)
                inside = ndimage.distance_transform_edt(land) * resolution
                outside = ndimage.distance_transform_edt(~land) * resolution
                sdf = inside - outside
                sdf = sdf[m:m+PAGE, m:m+PAGE]
                coast_q = np.clip(np.rint(sdf / 0.5), -32767, 32767).astype("<i2")

                base = f"pages/{cx}_{cy}"
                _write(out / f"{base}_province_coast.bin", province_page.astype("<u2", copy=False).tobytes() + coast_q.tobytes())
                manifest.append(f"province_coast 0 {cx} {cy} 0 {base}_province_coast.bin")
                spatial_mask = (province_page != 0).astype("uint8")
                _write(out / f"{base}_spatial_mask.bin", spatial_mask.tobytes())
                manifest.append(f"spatial_mask 0 {cx} {cy} 0 {base}_spatial_mask.bin")

                # Deterministic 4-km candidate grid. Extra classes are semantic hints; real authored
                # settlement/resource layers can supersede these without changing runtime format.
                records = []
                for py in range(4, PAGE, 8):
                    for px in range(4, PAGE, 8):
                        rid = int(province_page[py, px]) - 1
                        if rid < 0:
                            continue
                        lx = int((px + 0.5) * resolution)
                        ly = int(CHUNK_M - (py + 0.5) * resolution)
                        coast_m = abs(int(coast_q[py, px]) * 0.5)
                        rep = reps.iloc[rid]
                        ax, ay = x0 + lx, y0 + ly
                        d = math.hypot(ax - float(rep.x), ay - float(rep.y))
                        records.append((rid, lx, ly, 0, PLACEMENT_BUILDABLE, FLAG_BUILDABLE, 100))
                        if d <= 18_000:
                            records.append((rid, lx, ly, 0, PLACEMENT_URBAN, FLAG_BUILDABLE, 150))
                            if ((rid * 1315423911 + px * 31 + py) & 1) == 0:
                                records.append((rid, lx, ly, 0, PLACEMENT_INDUSTRIAL, FLAG_BUILDABLE, 90))
                        else:
                            records.append((rid, lx, ly, 0, PLACEMENT_RURAL, FLAG_BUILDABLE, 100))
                            records.append((rid, lx, ly, 0, PLACEMENT_FARM, FLAG_BUILDABLE, 80))
                            records.append((rid, lx, ly, 0, PLACEMENT_VEGETATION, FLAG_BUILDABLE, 70))
                        if coast_m <= 2_000:
                            records.append((rid, lx, ly, 0, PLACEMENT_PORT, FLAG_BUILDABLE | FLAG_COASTAL, 120))
                _write(out / f"{base}_placement.bin", _placement_payload(records))
                manifest.append(f"placement_candidates 0 {cx} {cy} 0 {base}_placement.bin")
                candidate_count += len(records)

                anchors = default_anchors.get((cx, cy), [])
                if anchors:
                    _write(out / f"{base}_anchors.bin", _anchor_payload(anchors))
                    manifest.append(f"settlement_anchors 0 {cx} {cy} 0 {base}_anchors.bin")

                if dem_vrt is not None:
                    window = rasterio.windows.from_bounds(x0, y0, x1, y1, transform=dem_vrt.transform)
                    heights = dem_vrt.read(1, window=window, out_shape=(65, 65), resampling=Resampling.bilinear, boundless=True, fill_value=0).astype(np.float64)
                    if dem_vrt.nodata is not None:
                        heights[heights == dem_vrt.nodata] = 0
                    heights = np.nan_to_num(heights, nan=0.0, posinf=0.0, neginf=0.0)
                    q = np.clip(np.rint(np.maximum(heights, 0.0) / 0.5), 0, 65535).astype("<u2")
                    _write(out / f"{base}_height.bin", q.tobytes())
                    manifest.append(f"height 0 {cx} {cy} 0 {base}_height.bin")
                page_count += 1
    finally:
        if dem_vrt is not None:
            dem_vrt.close()
        if dem_src is not None:
            dem_src.close()
    return manifest, page_count, candidate_count


def _authored_settlements(out: Path, path: Path | None, province_gdf, province_id):
    if path is None:
        return []
    points = gpd.read_file(path)
    if points.crs is None:
        raise ValueError("settlement layer has no CRS")
    points = points.to_crs(3857)
    if "province_key" not in points.columns:
        joined = gpd.sjoin(points, province_gdf[["province_key", "geometry"]], predicate="within", how="left")
        points["province_key"] = joined["province_key"].values
    if points.province_key.isna().any():
        raise ValueError("one or more settlements could not be assigned to a province")
    grouped: dict[tuple[int, int], list[tuple]] = {}
    for idx, row in points.iterrows():
        key = str(row.province_key)
        if key not in province_id:
            raise ValueError(f"settlement references unknown province_key {key}")
        x, y = float(row.geometry.x), float(row.geometry.y)
        cx, cy = math.floor(x / CHUNK_M), math.floor(y / CHUNK_M)
        lx, ly = int(round(x - cx * CHUNK_M)), int(round(y - cy * CHUNK_M))
        importance = int(row["importance"]) if "importance" in points.columns and not math.isnan(float(row["importance"])) else 1000
        name = str(row["key"]) if "key" in points.columns else f"settlement_{idx}"
        grouped.setdefault((cx, cy), []).append((province_id[key], min(max(lx,0),63999), min(max(ly,0),63999), 0, max(0,min(importance,65535)), _fnv1a64(name)))
    manifest = []
    for (cx, cy), records in sorted(grouped.items()):
        rel = f"authored/settlement_{cx}_{cy}.bin"
        _write(out / rel, _anchor_payload(records))
        # variant 1 coexists with the default-anchor variant 0 in the same world chunk coordinate.
        manifest.append(f"settlement_anchors 0 {cx} {cy} 1 {rel}")
    return manifest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--provinces", type=Path, required=True)
    ap.add_argument("--layer", default=None)
    ap.add_argument("--dem", type=Path)
    ap.add_argument("--settlements", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--resolution", type=float, default=DEFAULT_RESOLUTION_M)
    args = ap.parse_args()

    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    gdf = _repair_project(args.provinces, args.layer)
    ids = _ids(gdf)
    province_id = ids[-1]
    reps = _write_definitions(out, gdf, ids)
    edges = _adjacency(out, gdf)
    page_manifest, pages, candidates = _compile_pages(out, gdf, province_id, reps, args.resolution, args.dem)
    authored = _authored_settlements(out, args.settlements, gdf, province_id)

    manifest = [
        "# type level x y variant file",
        "country_definitions 0 0 0 0 definitions/countries.bin",
        "market_definitions 0 0 0 0 definitions/markets.bin",
        "state_definitions 0 0 0 0 definitions/states.bin",
        "province_definitions 0 0 0 0 definitions/provinces.bin",
        "adjacency_offsets 0 0 0 0 definitions/adjacency_offsets.bin",
        "adjacency_neighbors 0 0 0 0 definitions/adjacency_neighbors.bin",
    ] + page_manifest + authored
    (out / "manifest.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    metadata = {
        "format": "Core GIS compiler 1.0",
        "source": str(args.provinces),
        "crs_runtime": "EPSG:3857",
        "province_count": len(gdf),
        "country_count": len(ids[0]),
        "market_count": len(ids[2]),
        "state_count": len(ids[5]),
        "map_pages": pages,
        "placement_candidates": candidates,
        "adjacency_edges_undirected": edges,
        "pixel_resolution_m": args.resolution,
    }
    (out / "compile_report.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
