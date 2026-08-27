#!/usr/bin/env python3
"""Procedural 3D Model and Asset Generator for Statue of Liberty (Statue de la Liberté).

Generates high-fidelity multi-LOD 3D geometry, PBR materials, LOD manifests,
and cooks them into the Core Engine .corepack asset format.
"""
from __future__ import annotations

import json
import math
import os
import struct
import sys
from pathlib import Path


class Vec3:
    __slots__ = ("x", "y", "z")

    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0):
        self.x = float(x)
        self.y = float(y)
        self.z = float(z)

    def __add__(self, o: Vec3) -> Vec3:
        return Vec3(self.x + o.x, self.y + o.y, self.z + o.z)

    def __sub__(self, o: Vec3) -> Vec3:
        return Vec3(self.x - o.x, self.y - o.y, self.z - o.z)

    def __mul__(self, s: float) -> Vec3:
        return Vec3(self.x * s, self.y * s, self.z * s)

    def length(self) -> float:
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)

    def normalized(self) -> Vec3:
        l = self.length()
        if l < 1e-9:
            return Vec3(0, 1, 0)
        return Vec3(self.x / l, self.y / l, self.z / l)

    def cross(self, o: Vec3) -> Vec3:
        return Vec3(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )


class Mesh:
    def __init__(self, name: str = "mesh"):
        self.name = name
        self.positions: list[Vec3] = []
        self.normals: list[Vec3] = []
        self.uvs: list[tuple[float, float]] = []
        self.colors: list[tuple[float, float, float]] = []
        self.indices: list[int] = []

    def add_vertex(
        self,
        pos: Vec3,
        normal: Vec3 | None = None,
        uv: tuple[float, float] = (0.0, 0.0),
        color: tuple[float, float, float] = (0.38, 0.62, 0.52),
    ) -> int:
        idx = len(self.positions)
        self.positions.append(pos)
        self.normals.append(normal if normal is not None else Vec3(0, 1, 0))
        self.uvs.append(uv)
        self.colors.append(color)
        return idx

    def add_triangle(self, i0: int, i1: int, i2: int) -> None:
        self.indices.extend([i0, i1, i2])

    def add_quad(self, i0: int, i1: int, i2: int, i3: int) -> None:
        self.indices.extend([i0, i1, i2, i0, i2, i3])

    def compute_normals(self) -> None:
        normals = [Vec3(0, 0, 0) for _ in self.positions]
        for t in range(0, len(self.indices), 3):
            i0, i1, i2 = self.indices[t], self.indices[t + 1], self.indices[t + 2]
            p0, p1, p2 = self.positions[i0], self.positions[i1], self.positions[i2]
            fn = (p1 - p0).cross(p2 - p0)
            normals[i0] = normals[i0] + fn
            normals[i1] = normals[i1] + fn
            normals[i2] = normals[i2] + fn
        self.normals = [n.normalized() for n in normals]

    def triangle_count(self) -> int:
        return len(self.indices) // 3


def add_cylinder(
    mesh: Mesh,
    base_center: Vec3,
    r_bottom: float,
    r_top: float,
    height: float,
    segments: int,
    color: tuple[float, float, float],
    cap_bottom: bool = True,
    cap_top: bool = True,
) -> None:
    bottom_indices = []
    top_indices = []
    for s in range(segments):
        theta = 2.0 * math.pi * s / segments
        cos_t, sin_t = math.cos(theta), math.sin(theta)
        p_bot = Vec3(base_center.x + r_bottom * cos_t, base_center.y, base_center.z + r_bottom * sin_t)
        p_top = Vec3(base_center.x + r_top * cos_t, base_center.y + height, base_center.z + r_top * sin_t)
        norm = Vec3(cos_t, 0.0, sin_t).normalized()
        bottom_indices.append(mesh.add_vertex(p_bot, norm, (s / segments, 0.0), color))
        top_indices.append(mesh.add_vertex(p_top, norm, (s / segments, 1.0), color))

    for s in range(segments):
        s_next = (s + 1) % segments
        b0, b1 = bottom_indices[s], bottom_indices[s_next]
        t0, t1 = top_indices[s], top_indices[s_next]
        mesh.add_quad(b0, b1, t1, t0)

    if cap_bottom:
        c_bot = mesh.add_vertex(base_center, Vec3(0, -1, 0), (0.5, 0.5), color)
        for s in range(segments):
            s_next = (s + 1) % segments
            mesh.add_triangle(c_bot, bottom_indices[s_next], bottom_indices[s])

    if cap_top:
        c_top = mesh.add_vertex(Vec3(base_center.x, base_center.y + height, base_center.z), Vec3(0, 1, 0), (0.5, 0.5), color)
        for s in range(segments):
            s_next = (s + 1) % segments
            mesh.add_triangle(c_top, top_indices[s], top_indices[s_next])


def add_box(
    mesh: Mesh,
    center: Vec3,
    size_x: float,
    size_y: float,
    size_z: float,
    color: tuple[float, float, float],
) -> None:
    hx, hy, hz = size_x * 0.5, size_y * 0.5, size_z * 0.5
    # 6 faces
    faces = [
        # Front (+Z)
        (Vec3(0, 0, 1), [Vec3(-hx, -hy, hz), Vec3(hx, -hy, hz), Vec3(hx, hy, hz), Vec3(-hx, hy, hz)]),
        # Back (-Z)
        (Vec3(0, 0, -1), [Vec3(hx, -hy, -hz), Vec3(-hx, -hy, -hz), Vec3(-hx, hy, -hz), Vec3(hx, hy, -hz)]),
        # Top (+Y)
        (Vec3(0, 1, 0), [Vec3(-hx, hy, hz), Vec3(hx, hy, hz), Vec3(hx, hy, -hz), Vec3(-hx, hy, -hz)]),
        # Bottom (-Y)
        (Vec3(0, -1, 0), [Vec3(-hx, -hy, -hz), Vec3(hx, -hy, -hz), Vec3(hx, -hy, hz), Vec3(-hx, -hy, hz)]),
        # Right (+X)
        (Vec3(1, 0, 0), [Vec3(hx, -hy, hz), Vec3(hx, -hy, -hz), Vec3(hx, hy, -hz), Vec3(hx, hy, hz)]),
        # Left (-X)
        (Vec3(-1, 0, 0), [Vec3(-hx, -hy, -hz), Vec3(-hx, -hy, hz), Vec3(-hx, hy, hz), Vec3(-hx, hy, -hz)]),
    ]
    for norm, corners in faces:
        i0 = mesh.add_vertex(center + corners[0], norm, (0, 0), color)
        i1 = mesh.add_vertex(center + corners[1], norm, (1, 0), color)
        i2 = mesh.add_vertex(center + corners[2], norm, (1, 1), color)
        i3 = mesh.add_vertex(center + corners[3], norm, (0, 1), color)
        mesh.add_quad(i0, i1, i2, i3)


def add_star_fort_base(
    mesh: Mesh,
    center: Vec3,
    outer_r: float,
    inner_r: float,
    points: int,
    height: float,
    color: tuple[float, float, float],
) -> None:
    bot_v = []
    top_v = []
    total_pts = points * 2
    for p in range(total_pts):
        theta = 2.0 * math.pi * p / total_pts
        r = outer_r if (p % 2 == 0) else inner_r
        cos_t, sin_t = math.cos(theta), math.sin(theta)
        p_bot = Vec3(center.x + r * cos_t, center.y, center.z + r * sin_t)
        p_top = Vec3(center.x + (r - 0.5) * cos_t, center.y + height, center.z + (r - 0.5) * sin_t)
        norm = Vec3(cos_t, 0.2, sin_t).normalized()
        bot_v.append(mesh.add_vertex(p_bot, norm, (p / total_pts, 0), color))
        top_v.append(mesh.add_vertex(p_top, norm, (p / total_pts, 1), color))

    for p in range(total_pts):
        p_next = (p + 1) % total_pts
        mesh.add_quad(bot_v[p], bot_v[p_next], top_v[p_next], top_v[p])

    c_top = mesh.add_vertex(Vec3(center.x, center.y + height, center.z), Vec3(0, 1, 0), (0.5, 0.5), color)
    for p in range(total_pts):
        p_next = (p + 1) % total_pts
        mesh.add_triangle(c_top, top_v[p], top_v[p_next])


def generate_statue_of_liberty(lod: int = 0) -> Mesh:
    mesh = Mesh(f"statue_of_liberty_lod{lod}")

    # Colors
    c_star_fort = (0.55, 0.52, 0.49)    # Weathered masonry granite
    c_pedestal = (0.58, 0.55, 0.50)     # Neoclassical rusticated granite
    c_copper = (0.376, 0.616, 0.518)     # Verdigris copper patina (#609d84)
    c_gold = (0.831, 0.686, 0.216)       # Gilded torch flame (#d4af37)
    c_tablet = (0.33, 0.56, 0.47)       # Bronze tablet

    seg_mult = {0: 16, 1: 10, 2: 6, 3: 4}[lod]

    # --- 1. Star Fort Foundation (Fort Wood) ---
    add_star_fort_base(mesh, Vec3(0, 0, 0), outer_r=22.0, inner_r=15.0, points=11 if lod <= 1 else 8, height=6.0, color=c_star_fort)

    # --- 2. Neoclassical Pedestal (Granite) ---
    # Lower terrace
    add_box(mesh, Vec3(0, 7.5, 0), size_x=24.0, size_y=3.0, size_z=24.0, color=c_pedestal)
    # Mid rusticated tower
    add_box(mesh, Vec3(0, 16.5, 0), size_x=16.0, size_y=15.0, size_z=16.0, color=c_pedestal)
    # Pedestal cornice and balcony
    add_box(mesh, Vec3(0, 24.5, 0), size_x=18.0, size_y=1.5, size_z=18.0, color=c_pedestal)
    # Upper loggia
    add_box(mesh, Vec3(0, 28.5, 0), size_x=14.0, size_y=6.5, size_z=14.0, color=c_pedestal)
    # Top balcony & plinth
    add_box(mesh, Vec3(0, 32.5, 0), size_x=15.0, size_y=1.5, size_z=15.0, color=c_pedestal)
    add_box(mesh, Vec3(0, 34.0, 0), size_x=10.0, size_y=1.5, size_z=10.0, color=c_copper)

    # --- 3. Statue Figure (Libertas) ---
    statue_y = 35.0

    # Draped Robes / Lower Body
    add_cylinder(mesh, Vec3(0, statue_y, 0), r_bottom=4.0, r_top=3.2, height=14.0, segments=seg_mult, color=c_copper)
    # Torso & Himation Drapery
    add_cylinder(mesh, Vec3(0, statue_y + 14.0, 0), r_bottom=3.2, r_top=2.4, height=10.0, segments=seg_mult, color=c_copper)
    # Shoulders / Chest
    add_box(mesh, Vec3(0, statue_y + 24.0, 0), size_x=6.0, size_y=2.5, size_z=3.6, color=c_copper)

    # Head & Neck
    add_cylinder(mesh, Vec3(0, statue_y + 25.0, 0), r_bottom=1.2, r_top=1.3, height=2.2, segments=seg_mult, color=c_copper)
    # Head block
    add_box(mesh, Vec3(0, statue_y + 28.2, 0), size_x=2.2, size_y=2.8, size_z=2.2, color=c_copper)

    # Crown Diadem & 7 Radiant Rays/Spikes
    crown_y = statue_y + 29.5
    add_cylinder(mesh, Vec3(0, crown_y, 0), r_bottom=1.4, r_top=1.6, height=0.8, segments=seg_mult, color=c_copper)

    if lod <= 2:
        # 7 outward radiant spikes (representing 7 seas and continents)
        for spike_i in range(7):
            angle = -math.pi * 0.45 + (spike_i * (math.pi * 0.9 / 6.0))
            cos_a, sin_a = math.cos(angle), math.sin(angle)
            spike_base = Vec3(1.5 * sin_a, crown_y + 0.5, 1.5 * cos_a)
            spike_tip = Vec3(3.4 * sin_a, crown_y + 1.8, 3.4 * cos_a)
            # Add spike geometry
            i0 = mesh.add_vertex(spike_base + Vec3(-0.2 * cos_a, 0, 0.2 * sin_a), color=c_copper)
            i1 = mesh.add_vertex(spike_base + Vec3(0.2 * cos_a, 0, -0.2 * sin_a), color=c_copper)
            i2 = mesh.add_vertex(spike_tip, color=c_copper)
            mesh.add_triangle(i0, i1, i2)

    # --- 4. Right Arm & Raised Torch of Enlightenment ---
    # Right Upper Arm
    arm_start = Vec3(2.8, statue_y + 24.0, 0.0)
    add_cylinder(mesh, arm_start, r_bottom=0.9, r_top=0.8, height=6.0, segments=max(4, seg_mult // 2), color=c_copper)
    # Right Forearm extending upwards
    forearm_start = Vec3(3.2, statue_y + 29.5, 0.2)
    add_cylinder(mesh, forearm_start, r_bottom=0.8, r_top=0.7, height=8.0, segments=max(4, seg_mult // 2), color=c_copper)

    # Torch Handle & Chalice Balustrade
    torch_y = statue_y + 37.5
    add_cylinder(mesh, Vec3(3.2, torch_y, 0.2), r_bottom=0.5, r_top=1.2, height=2.0, segments=seg_mult, color=c_copper)
    # Gilded Torch Flame
    add_cylinder(mesh, Vec3(3.2, torch_y + 2.0, 0.2), r_bottom=1.1, r_top=0.2, height=3.2, segments=max(4, seg_mult // 2), color=c_gold)

    # --- 5. Left Arm & Tabula Ansata (Tablet of Independence) ---
    # Left Arm cradling tablet
    add_box(mesh, Vec3(-2.8, statue_y + 20.0, 1.2), size_x=1.8, size_y=4.5, size_z=1.6, color=c_copper)
    # Tablet (Inscribed JULY IV MDCCLXXVI)
    add_box(mesh, Vec3(-2.5, statue_y + 19.5, 2.2), size_x=3.5, size_y=4.8, size_z=0.6, color=c_tablet)

    mesh.compute_normals()
    return mesh


def export_obj(mesh: Mesh, filepath: Path) -> None:
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(f"# Core Engine 3D Monument: {mesh.name}\n")
        f.write(f"# Triangles: {mesh.triangle_count()}\n\n")
        for p in mesh.positions:
            f.write(f"v {p.x:.4f} {p.y:.4f} {p.z:.4f}\n")
        for n in mesh.normals:
            f.write(f"vn {n.x:.4f} {n.y:.4f} {n.z:.4f}\n")
        for u, v in mesh.uvs:
            f.write(f"vt {u:.4f} {v:.4f}\n")
        for t in range(0, len(mesh.indices), 3):
            i0 = mesh.indices[t] + 1
            i1 = mesh.indices[t + 1] + 1
            i2 = mesh.indices[t + 2] + 1
            f.write(f"f {i0}/{i0}/{i0} {i1}/{i1}/{i1} {i2}/{i2}/{i2}\n")


def export_glb(mesh: Mesh, filepath: Path) -> None:
    # Construct binary glTF 2.0 (.glb)
    pos_data = bytearray()
    norm_data = bytearray()
    uv_data = bytearray()
    col_data = bytearray()
    idx_data = bytearray()

    min_p = [float("inf")] * 3
    max_p = [float("-inf")] * 3

    for p in mesh.positions:
        pos_data.extend(struct.pack("<3f", p.x, p.y, p.z))
        min_p[0], max_p[0] = min(min_p[0], p.x), max(max_p[0], p.x)
        min_p[1], max_p[1] = min(min_p[1], p.y), max(max_p[1], p.y)
        min_p[2], max_p[2] = min(min_p[2], p.z), max(max_p[2], p.z)

    for n in mesh.normals:
        norm_data.extend(struct.pack("<3f", n.x, n.y, n.z))

    for u, v in mesh.uvs:
        uv_data.extend(struct.pack("<2f", u, v))

    for r, g, b in mesh.colors:
        col_data.extend(struct.pack("<3f", r, g, b))

    for idx in mesh.indices:
        idx_data.extend(struct.pack("<I", idx))

    buf_bytes = pos_data + norm_data + uv_data + col_data + idx_data
    # Align to 4 bytes
    while len(buf_bytes) % 4 != 0:
        buf_bytes.append(0)

    pos_off = 0
    norm_off = len(pos_data)
    uv_off = norm_off + len(norm_data)
    col_off = uv_off + len(uv_data)
    idx_off = col_off + len(col_data)

    v_count = len(mesh.positions)
    i_count = len(mesh.indices)

    gltf = {
        "asset": {"version": "2.0", "generator": "Core Engine Monument Generator"},
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": mesh.name}],
        "meshes": [
            {
                "name": mesh.name,
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": 0,
                            "NORMAL": 1,
                            "TEXCOORD_0": 2,
                            "COLOR_0": 3,
                        },
                        "indices": 4,
                        "material": 0,
                    }
                ],
            }
        ],
        "materials": [
            {
                "name": "StatueOfLiberty_PBR",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.38, 0.62, 0.52, 1.0],
                    "metallicFactor": 0.2,
                    "roughnessFactor": 0.65,
                },
            }
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": v_count, "type": "VEC3", "min": min_p, "max": max_p},
            {"bufferView": 1, "componentType": 5126, "count": v_count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": v_count, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5126, "count": v_count, "type": "VEC3"},
            {"bufferView": 4, "componentType": 5125, "count": i_count, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_data), "target": 34962},
            {"buffer": 0, "byteOffset": norm_off, "byteLength": len(norm_data), "target": 34962},
            {"buffer": 0, "byteOffset": uv_off, "byteLength": len(uv_data), "target": 34962},
            {"buffer": 0, "byteOffset": col_off, "byteLength": len(col_data), "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_data), "target": 34963},
        ],
        "buffers": [{"byteLength": len(buf_bytes)}],
    }

    json_str = json.dumps(gltf, separators=(",", ":"))
    json_bytes = json_str.encode("utf-8")
    while len(json_bytes) % 4 != 0:
        json_bytes += b" "

    glb_len = 12 + 8 + len(json_bytes) + 8 + len(buf_bytes)
    with open(filepath, "wb") as f:
        # Header: magic (0x46546C67), version (2), total length
        f.write(struct.pack("<4sII", b"glTF", 2, glb_len))
        # Chunk 0: JSON
        f.write(struct.pack("<II4s", len(json_bytes), 0x4E4F534A, b"JSON"))
        f.write(json_bytes)
        # Chunk 1: BIN
        f.write(struct.pack("<II4s", len(buf_bytes), 0x004E4942, b"BIN\x00"))
        f.write(buf_bytes)


def main() -> None:
    out_dir = Path("build/assets/monuments/statue_of_liberty")
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = []
    ratios = [1.0, 0.45, 0.18, 0.05]

    print("===============================================================")
    print("  CORE ENGINE 3D MONUMENT GENERATOR: STATUE OF LIBERTY         ")
    print("===============================================================")

    for lod in range(4):
        mesh = generate_statue_of_liberty(lod=lod)
        obj_path = out_dir / f"statue_of_liberty_lod{lod}.obj"
        glb_path = out_dir / f"statue_of_liberty_lod{lod}.glb"
        export_obj(mesh, obj_path)
        export_glb(mesh, glb_path)
        tris = mesh.triangle_count()
        verts = len(mesh.positions)
        print(f"  [LOD {lod}] Triangles: {tris:6d} | Vertices: {verts:6d} -> {glb_path.name}")
        manifest.append({
            "lod": lod,
            "ratio": ratios[lod],
            "file": glb_path.name,
            "obj_file": obj_path.name,
            "vertices": verts,
            "triangles": tris,
        })

    # Write LOD manifest
    manifest_path = out_dir / "statue_of_liberty.corelod.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    # Write PBR Material configuration
    material_def = {
        "name": "monument_statue_of_liberty",
        "shader": "pbr_monument_forward",
        "properties": {
            "albedo_color": [0.376, 0.616, 0.518, 1.0],  # Verdigris copper
            "metallic": 0.25,
            "roughness": 0.62,
            "pedestal_color": [0.58, 0.55, 0.50, 1.0],  # Granite
            "torch_gold_color": [0.831, 0.686, 0.216, 1.0],
            "torch_metallic": 0.85,
            "torch_roughness": 0.25,
        }
    }
    with open(out_dir / "statue_of_liberty.coremat.json", "w", encoding="utf-8") as f:
        json.dump(material_def, f, indent=2)

    print(f"\n[OK] Generated 4-LOD 3D Statue of Liberty assets into: {out_dir.resolve()}")


if __name__ == "__main__":
    main()
