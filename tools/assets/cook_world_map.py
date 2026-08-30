#!/usr/bin/env python3
"""Cook workspace-authored country label paths into the standard world map index.

The cooker intentionally has no source-path arguments.  Every input and output
is resolved below this repository so a desktop build can never acquire an
implicit dependency on another checkout.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct


PROJECT_ROOT = Path(__file__).resolve().parents[2]
WORLD_DIRECTORY = PROJECT_ROOT / "content" / "base" / "map" / "world"
MAP_PATH = WORLD_DIRECTORY / "world_locations.coremap"
LABEL_PATH = WORLD_DIRECTORY / "country_labels.json"
MANIFEST_PATH = WORLD_DIRECTORY / "world_map_manifest.json"
MAGIC = b"COREMAP1"
OUTPUT_VERSION = 3


class BinaryReader:
    def __init__(self, payload: bytes) -> None:
        self.payload = payload
        self.offset = 0

    def read(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.payload):
            raise ValueError("truncated world map index")
        values = struct.unpack_from(fmt, self.payload, self.offset)
        self.offset += size
        return values[0] if len(values) == 1 else values

    def skip(self, size: int) -> None:
        if size < 0 or self.offset + size > len(self.payload):
            raise ValueError("truncated world map index")
        self.offset += size

    def text(self) -> str:
        size = self.read("<H")
        if self.offset + size > len(self.payload):
            raise ValueError("truncated world map text")
        value = self.payload[self.offset : self.offset + size].decode("utf-8")
        self.offset += size
        return value


def write_text(stream, value: str) -> None:
    encoded = value.encode("utf-8")
    if len(encoded) > 65_535:
        raise ValueError("world map label is too long")
    stream.write(struct.pack("<H", len(encoded)))
    stream.write(encoded)


def parse_index(payload: bytes) -> tuple[int, int, list[dict[str, object]]]:
    reader = BinaryReader(payload)
    if payload[:8] != MAGIC:
        raise ValueError("invalid world map index magic")
    reader.skip(8)
    version = reader.read("<I")
    if version not in (2, 3):
        raise ValueError(f"unsupported world map index version: {version}")
    width, height = reader.read("<II")
    if width <= 0 or height <= 0:
        raise ValueError("invalid world map dimensions")
    reader.skip(struct.calcsize("<4d"))

    row_count = reader.read("<I")
    if row_count != height:
        raise ValueError("world map row count does not match height")
    for _ in range(row_count):
        run_count = reader.read("<I")
        if run_count <= 0 or run_count > width:
            raise ValueError("invalid world map scanline")
        reader.skip(run_count * struct.calcsize("<HH"))

    location_count = reader.read("<I")
    if location_count > 65_535:
        raise ValueError("too many world map locations")
    for _ in range(location_count):
        reader.skip(struct.calcsize("<HfffQf"))
        reader.text()
        reader.text()

    labels_offset = reader.offset
    label_count = reader.read("<I")
    labels: list[dict[str, object]] = []
    for _ in range(label_count):
        u, v, priority, area = reader.read("<ffBf")
        if version >= 3:
            reader.read("<f")
            point_count = reader.read("<B")
            if point_count < 2 or point_count > 64:
                raise ValueError("invalid country label path")
            reader.skip(point_count * struct.calcsize("<ff"))
        labels.append({
            "u": float(u),
            "v": float(v),
            "priority": int(priority),
            "area": float(area),
            "text": reader.text(),
        })
    if reader.offset != len(payload):
        raise ValueError("world map index has trailing data")
    return version, labels_offset, labels


def wrapped_distance_sq(left: tuple[float, float], right: tuple[float, float]) -> float:
    du = abs(left[0] - right[0])
    du = min(du, 1.0 - du)
    dv = left[1] - right[1]
    return du * du + dv * dv


def load_authored_labels() -> list[dict[str, object]]:
    document = json.loads(LABEL_PATH.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported country label schema")
    labels = document.get("country_labels")
    if not isinstance(labels, list):
        raise ValueError("country label document has no country_labels array")
    return labels


def match_labels(index_labels: list[dict[str, object]],
                 authored_labels: list[dict[str, object]]) -> list[dict[str, object]]:
    by_text: dict[str, list[int]] = {}
    for index, label in enumerate(authored_labels):
        text = str(label.get("label_text") or label.get("name") or "")
        by_text.setdefault(text, []).append(index)

    used: set[int] = set()
    matched: list[dict[str, object]] = []
    for index_label in index_labels:
        text = str(index_label["text"])
        candidates = [index for index in by_text.get(text, []) if index not in used]
        if not candidates:
            raise ValueError(f"missing authored label metadata for {text!r}")
        anchor = (float(index_label["u"]), float(index_label["v"]))
        selected = min(
            candidates,
            key=lambda index: wrapped_distance_sq(
                anchor,
                tuple(float(value) for value in authored_labels[index]["anchor_uv"]),
            ),
        )
        if wrapped_distance_sq(
            anchor,
            tuple(float(value) for value in authored_labels[selected]["anchor_uv"]),
        ) > 0.01:
            raise ValueError(f"authored label anchor is inconsistent for {text!r}")
        used.add(selected)
        matched.append(authored_labels[selected])

    if len(used) != len(authored_labels):
        raise ValueError("country label document contains unmatched labels")
    return matched


def validate_point(value: object, label: str) -> tuple[float, float]:
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{label} must be a two-component UV point")
    point = (float(value[0]), float(value[1]))
    if not (0.0 <= point[0] <= 1.0 and 0.0 <= point[1] <= 1.0):
        raise ValueError(f"{label} lies outside the world map")
    return point


def cook_index(payload: bytes, labels_offset: int,
               index_labels: list[dict[str, object]],
               authored_labels: list[dict[str, object]]) -> bytes:
    priority_values = {"minor": 0, "standard": 1, "major": 2}
    output = bytearray(payload[:labels_offset])
    struct.pack_into("<I", output, 8, OUTPUT_VERSION)
    from io import BytesIO

    tail = BytesIO()
    tail.write(struct.pack("<I", len(index_labels)))
    for old, authored in zip(index_labels, match_labels(index_labels, authored_labels), strict=True):
        anchor = validate_point(authored.get("anchor_uv"), "country label anchor")
        raw_spine = authored.get("label_spine_uv")
        if not isinstance(raw_spine, list) or not 2 <= len(raw_spine) <= 64:
            raise ValueError(f"invalid country label path for {old['text']!r}")
        spine = [validate_point(point, "country label path point") for point in raw_spine]
        priority_name = str(authored.get("label_priority") or "minor")
        if priority_name not in priority_values:
            raise ValueError(f"invalid country label priority for {old['text']!r}")
        text = str(authored.get("label_text") or authored.get("name") or "")
        if text != old["text"]:
            raise ValueError(f"country label text mismatch for {old['text']!r}")
        tail.write(struct.pack(
            "<ffBffB",
            anchor[0],
            anchor[1],
            priority_values[priority_name],
            float(authored.get("component_area_km2", old["area"])),
            float(authored.get("axis_angle_degrees", 0.0)),
            len(spine),
        ))
        for point in spine:
            tail.write(struct.pack("<ff", *point))
        write_text(tail, text)
    output.extend(tail.getvalue())
    return bytes(output)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def update_manifest() -> None:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    manifest.pop("source", None)
    manifest.pop("source_map_build_id", None)
    manifest.pop("source_politics_build_id", None)
    manifest["location_index_format_version"] = OUTPUT_VERSION
    manifest.setdefault("outputs", {})[MAP_PATH.name] = sha256(MAP_PATH)
    temporary = MANIFEST_PATH.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, MANIFEST_PATH)


def main() -> int:
    for path in (MAP_PATH, LABEL_PATH, MANIFEST_PATH):
        if not path.is_file():
            raise FileNotFoundError(f"missing workspace world-map input: {path.relative_to(PROJECT_ROOT)}")

    payload = MAP_PATH.read_bytes()
    _, labels_offset, index_labels = parse_index(payload)
    authored_labels = load_authored_labels()
    cooked = cook_index(payload, labels_offset, index_labels, authored_labels)
    version, _, verified_labels = parse_index(cooked)
    if version != OUTPUT_VERSION or len(verified_labels) != len(index_labels):
        raise ValueError("cooked world map failed verification")

    temporary = MAP_PATH.with_suffix(".coremap.tmp")
    temporary.write_bytes(cooked)
    os.replace(temporary, MAP_PATH)
    update_manifest()
    print(f"COOKED {MAP_PATH.relative_to(PROJECT_ROOT)} version={OUTPUT_VERSION} labels={len(index_labels)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
