#!/usr/bin/env python3
"""Suggest parallax layer masks for Grim backdrops from color + depth inputs.

This tool is intentionally conservative. It does not try to produce final
shipping masks automatically; it produces depth-band layer suggestions and
foreground-occluder candidates that are suitable for manual cleanup.

Supported inputs:
- Native Grim `.bm` and `.zbm` files (codec 0 and codec 3)
- Standard image files such as `.png` / `.tga`
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Iterable

import cv2
import numpy as np
from PIL import Image


@dataclass
class GrimBitmap:
    width: int
    height: int
    x: int
    y: int
    frames: list[np.ndarray]
    raw565: list[np.ndarray]


@dataclass
class LabMember:
    archive_path: Path
    member_name: str
    offset: int
    size: int


@dataclass
class LoadedRoom:
    color: np.ndarray
    depth: np.ndarray
    origin_x: int
    origin_y: int


def _read_uint32_be(stream: BytesIO) -> int:
    return struct.unpack(">I", stream.read(4))[0]


def _read_uint32_le(stream: BytesIO) -> int:
    return struct.unpack("<I", stream.read(4))[0]


def _decompress_codec3(compressed: bytes, expected_size: int) -> bytes:
    src_index = 0
    bitstr_value = struct.unpack_from("<H", compressed, src_index)[0]
    bitstr_len = 16
    src_index += 2

    def get_bit() -> int:
        nonlocal bitstr_value, bitstr_len, src_index
        bit = bitstr_value & 1
        bitstr_value >>= 1
        bitstr_len -= 1
        if bitstr_len == 0:
            bitstr_value = struct.unpack_from("<H", compressed, src_index)[0]
            bitstr_len = 16
            src_index += 2
        return bit

    output = bytearray(expected_size)
    out_index = 0

    while True:
        if get_bit() == 1:
            if out_index >= expected_size:
                raise ValueError("codec3 literal write overflow")
            output[out_index] = compressed[src_index]
            src_index += 1
            out_index += 1
            continue

        if get_bit() == 0:
            copy_len = 2 * get_bit()
            copy_len += get_bit() + 3
            copy_offset = compressed[src_index] - 0x100
            src_index += 1
        else:
            b0 = compressed[src_index]
            b1 = compressed[src_index + 1]
            copy_offset = (b0 | ((b1 & 0xF0) << 4)) - 0x1000
            copy_len = (b1 & 0x0F) + 3
            src_index += 2
            if copy_len == 3:
                copy_len = compressed[src_index] + 1
                src_index += 1
                if copy_len == 1:
                    return bytes(output[:out_index])

        while copy_len > 0:
            if out_index >= expected_size:
                raise ValueError("codec3 back-reference overflow")
            back_index = out_index + copy_offset
            if back_index < 0 or back_index >= expected_size:
                raise ValueError("codec3 invalid back-reference")
            output[out_index] = output[back_index]
            out_index += 1
            copy_len -= 1


def _load_grim_bitmap(path: Path) -> GrimBitmap:
    data = path.read_bytes()
    return _load_grim_bitmap_bytes(data, str(path))


def _load_grim_bitmap_bytes(data: bytes, source_name: str) -> GrimBitmap:
    stream = BytesIO(data)

    if _read_uint32_be(stream) != struct.unpack(">I", b"BM  ")[0]:
        raise ValueError(f"{source_name} is not a Grim BM file")
    if _read_uint32_be(stream) != struct.unpack(">I", b"F\0\0\0")[0]:
        raise ValueError(f"{source_name} has an unexpected Grim BM header")

    codec = _read_uint32_le(stream)
    stream.read(4)  # paletteIncluded
    num_images = _read_uint32_le(stream)
    x = _read_uint32_le(stream)
    y = _read_uint32_le(stream)
    stream.read(4)  # transparentColor
    stream.read(4)  # format
    bpp = _read_uint32_le(stream)
    if bpp != 16:
        raise ValueError(f"{source_name} uses unsupported {bpp}bpp format")

    stream.seek(128)
    width = _read_uint32_le(stream)
    height = _read_uint32_le(stream)

    frames: list[np.ndarray] = []
    raw565: list[np.ndarray] = []
    stream.seek(0x80)
    expected_size = (bpp // 8) * width * height
    for _ in range(num_images):
        stream.seek(8, os.SEEK_CUR)
        if codec == 0:
            frame_bytes = stream.read(expected_size)
        elif codec == 3:
            compressed_len = _read_uint32_le(stream)
            frame_bytes = _decompress_codec3(stream.read(compressed_len), expected_size)
        else:
            raise ValueError(f"{source_name} uses unsupported Grim bitmap codec {codec}")

        frame565 = np.frombuffer(frame_bytes, dtype="<u2").reshape((height, width)).copy()
        raw565.append(frame565)
        r = ((frame565 >> 11) & 0x1F).astype(np.uint8) * 255 // 31
        g = ((frame565 >> 5) & 0x3F).astype(np.uint8) * 255 // 63
        b = (frame565 & 0x1F).astype(np.uint8) * 255 // 31
        frames.append(np.dstack((r, g, b)))

    return GrimBitmap(width=width, height=height, x=x, y=y, frames=frames, raw565=raw565)


def _read_lab_entries(lab_path: Path) -> list[LabMember]:
    data = lab_path.read_bytes()
    stream = BytesIO(data)
    if _read_uint32_be(stream) != struct.unpack(">I", b"LABN")[0]:
        raise ValueError(f"{lab_path} is not a Grim LAB archive")
    _read_uint32_le(stream)  # version
    entry_count = _read_uint32_le(stream)
    string_table_size = _read_uint32_le(stream)
    string_table_offset = 16 * (entry_count + 1)
    string_table = data[string_table_offset:string_table_offset + string_table_size]

    entries: list[LabMember] = []
    stream.seek(16)
    for _ in range(entry_count):
        name_offset = _read_uint32_le(stream)
        start = _read_uint32_le(stream)
        size = _read_uint32_le(stream)
        _read_uint32_le(stream)

        end = string_table.find(b"\0", name_offset)
        if end == -1:
            end = len(string_table)
        member_name = string_table[name_offset:end].decode("latin-1").lower()
        entries.append(LabMember(lab_path, member_name, start, size))
    return entries


def _extract_lab_member(lab_path: Path, member_name: str) -> bytes:
    member_name = member_name.replace("\\", "/").lower()
    for entry in _read_lab_entries(lab_path):
        if entry.member_name == member_name:
            with lab_path.open("rb") as handle:
                handle.seek(entry.offset)
                return handle.read(entry.size)
    raise FileNotFoundError(f"{member_name} not found in {lab_path}")


def _resolve_input_path(path_spec: str) -> tuple[Path | None, str | None]:
    if "::" in path_spec:
        archive, member = path_spec.split("::", 1)
        return Path(archive), member.replace("\\", "/").lower()
    return Path(path_spec), None


def _load_color_image(path: Path) -> np.ndarray:
    if path.suffix.lower() in {".bm", ".zbm"}:
        grim = _load_grim_bitmap(path)
        return grim.frames[0]

    image = Image.open(path).convert("RGB")
    return np.asarray(image, dtype=np.uint8)


def _load_depth_image(path: Path) -> np.ndarray:
    if path.suffix.lower() in {".bm", ".zbm"}:
        grim = _load_grim_bitmap(path)
        return grim.raw565[0].astype(np.uint16)

    image = Image.open(path)
    array = np.asarray(image)
    if array.ndim == 3:
        if array.shape[2] >= 3:
            array = array[:, :, 0].astype(np.uint16) << 8 | array[:, :, 1].astype(np.uint16)
        else:
            array = array[:, :, 0]
    return array.astype(np.uint16 if array.dtype.itemsize > 1 else np.uint8)


def _load_color_spec(path_spec: str) -> np.ndarray:
    path, member = _resolve_input_path(path_spec)
    if member is not None:
        assert path is not None
        if path.suffix.lower() != ".lab":
            raise ValueError("archive member syntax requires a .lab file path")
        if member.endswith((".bm", ".zbm")):
            grim = _load_grim_bitmap_bytes(_extract_lab_member(path, member), f"{path}::{member}")
            return grim.frames[0]
        image = Image.open(BytesIO(_extract_lab_member(path, member))).convert("RGB")
        return np.asarray(image, dtype=np.uint8)
    assert path is not None
    return _load_color_image(path)


def _load_depth_spec(path_spec: str) -> np.ndarray:
    path, member = _resolve_input_path(path_spec)
    if member is not None:
        assert path is not None
        if path.suffix.lower() != ".lab":
            raise ValueError("archive member syntax requires a .lab file path")
        if member.endswith((".bm", ".zbm")):
            grim = _load_grim_bitmap_bytes(_extract_lab_member(path, member), f"{path}::{member}")
            return grim.raw565[0].astype(np.uint16)
        image = Image.open(BytesIO(_extract_lab_member(path, member)))
        array = np.asarray(image)
        if array.ndim == 3 and array.shape[2] >= 2:
            array = array[:, :, 0].astype(np.uint16) << 8 | array[:, :, 1].astype(np.uint16)
        return array.astype(np.uint16 if array.dtype.itemsize > 1 else np.uint8)
    assert path is not None
    return _load_depth_image(path)


def _load_color_spec_with_meta(path_spec: str) -> tuple[np.ndarray, int, int]:
    path, member = _resolve_input_path(path_spec)
    if member is not None:
        assert path is not None
        if path.suffix.lower() != ".lab":
            raise ValueError("archive member syntax requires a .lab file path")
        if member.endswith((".bm", ".zbm")):
            grim = _load_grim_bitmap_bytes(_extract_lab_member(path, member), f"{path}::{member}")
            return grim.frames[0], grim.x, grim.y
        image = Image.open(BytesIO(_extract_lab_member(path, member))).convert("RGB")
        return np.asarray(image, dtype=np.uint8), 0, 0

    assert path is not None
    if path.suffix.lower() in {".bm", ".zbm"}:
        grim = _load_grim_bitmap(path)
        return grim.frames[0], grim.x, grim.y
    return _load_color_image(path), 0, 0


def load_room_inputs(color_spec: str, depth_spec: str) -> LoadedRoom:
    color, origin_x, origin_y = _load_color_spec_with_meta(color_spec)
    depth = _load_depth_spec(depth_spec)
    if color.shape[:2] != depth.shape[:2]:
        raise ValueError(f"color image {color.shape[:2]} and depth image {depth.shape[:2]} differ")
    return LoadedRoom(color=color, depth=depth, origin_x=origin_x, origin_y=origin_y)


def _normalize_depth(depth: np.ndarray) -> np.ndarray:
    depth = depth.astype(np.float32)
    max_value = float(depth.max())
    if max_value <= 0.0:
        raise ValueError("depth image contains no usable depth values")
    return depth / max_value


def _compute_band_edges(depth_values: np.ndarray, layers: int) -> np.ndarray:
    quantiles = np.linspace(0.0, 1.0, layers + 1, dtype=np.float32)
    edges = np.quantile(depth_values, quantiles)
    unique_edges = [edges[0]]
    for edge in edges[1:]:
        if edge - unique_edges[-1] > 1e-4:
            unique_edges.append(edge)
    if len(unique_edges) < 2:
        raise ValueError("depth map is too flat to split into layers")
    return np.asarray(unique_edges, dtype=np.float32)


def _colorize_labels(labels: np.ndarray, background_color: tuple[int, int, int]) -> np.ndarray:
    palette = np.asarray(
        [
            background_color,
            (239, 71, 111),
            (255, 209, 102),
            (6, 214, 160),
            (17, 138, 178),
            (7, 59, 76),
            (131, 56, 236),
            (58, 134, 255),
            (251, 133, 0),
            (138, 201, 38),
        ],
        dtype=np.uint8,
    )
    colorized = np.zeros((*labels.shape, 3), dtype=np.uint8)
    max_label = int(labels.max())
    for idx in range(max_label + 1):
        colorized[labels == idx] = palette[idx % len(palette)]
    return colorized


def _save_mask(mask: np.ndarray, path: Path) -> None:
    Image.fromarray(mask.astype(np.uint8) * 255).save(path)


def _write_preview(color: np.ndarray, labels: np.ndarray, out_path: Path, alpha: float = 0.45) -> None:
    overlay = _colorize_labels(labels, (0, 0, 0))
    blended = np.clip(color.astype(np.float32) * (1.0 - alpha) + overlay.astype(np.float32) * alpha, 0.0, 255.0)
    Image.fromarray(blended.astype(np.uint8)).save(out_path)


def _component_boundary(mask: np.ndarray) -> np.ndarray:
    kernel = np.ones((3, 3), dtype=np.uint8)
    dilated = cv2.dilate(mask.astype(np.uint8), kernel, iterations=1)
    eroded = cv2.erode(mask.astype(np.uint8), kernel, iterations=1)
    return (dilated.astype(bool) & ~eroded.astype(bool))


def _ring_neighbors(mask: np.ndarray, radius: int = 4) -> np.ndarray:
    kernel = np.ones((radius * 2 + 1, radius * 2 + 1), dtype=np.uint8)
    dilated = cv2.dilate(mask.astype(np.uint8), kernel, iterations=1).astype(bool)
    return dilated & ~mask


def _component_bbox(mask: np.ndarray) -> tuple[int, int, int, int]:
    ys, xs = np.nonzero(mask)
    x0 = int(xs.min())
    y0 = int(ys.min())
    x1 = int(xs.max()) + 1
    y1 = int(ys.max()) + 1
    return x0, y0, x1 - x0, y1 - y0


def _rank_components(
    depth_norm: np.ndarray,
    labels: np.ndarray,
    band_index: int,
    min_region_pixels: int,
    max_components_per_band: int,
    out_dir: Path,
) -> list[dict]:
    band_mask = labels == band_index
    if not np.any(band_mask):
        return []

    num_components, component_labels, stats, _ = cv2.connectedComponentsWithStats(band_mask.astype(np.uint8), connectivity=8)
    components = []
    for component_id in range(1, num_components):
        area = int(stats[component_id, cv2.CC_STAT_AREA])
        if area < min_region_pixels:
            continue

        component_mask = component_labels == component_id
        component_depth = float(np.median(depth_norm[component_mask]))
        neighbor_mask = _ring_neighbors(component_mask)
        neighbor_depth = float(np.median(depth_norm[neighbor_mask])) if np.any(neighbor_mask) else component_depth
        boundary_mask = _component_boundary(component_mask)
        boundary_depth = depth_norm[boundary_mask] if np.any(boundary_mask) else depth_norm[component_mask]
        depth_edge_strength = float(np.median(np.abs(boundary_depth - component_depth)))

        # Lower depth values are closer in Grim's z-buffer space.
        occluder_depth_gap = max(0.0, neighbor_depth - component_depth)
        area_score = min(1.0, area / float(component_mask.shape[0] * component_mask.shape[1] * 0.15))
        score = occluder_depth_gap * 0.65 + depth_edge_strength * 0.25 + area_score * 0.10
        bbox = _component_bbox(component_mask)

        components.append(
            {
                "score": round(score, 6),
                "area": area,
                "bbox": list(bbox),
                "median_depth": round(component_depth, 6),
                "neighbor_depth": round(neighbor_depth, 6),
                "occluder_depth_gap": round(occluder_depth_gap, 6),
                "depth_edge_strength": round(depth_edge_strength, 6),
                "mask": component_mask,
            }
        )

    components.sort(key=lambda item: item["score"], reverse=True)
    kept = components[:max_components_per_band]
    for index, component in enumerate(kept, start=1):
        mask_path = out_dir / f"layer_{band_index:02d}_component_{index:02d}.png"
        _save_mask(component["mask"], mask_path)
        component["mask_path"] = mask_path.name
        del component["mask"]
    return kept


def suggest_layers(
    color: np.ndarray,
    depth: np.ndarray,
    layers_requested: int,
    min_region_pixels: int,
    max_components_per_band: int,
    out_dir: Path,
) -> dict:
    if color.shape[:2] != depth.shape[:2]:
        raise ValueError(f"color image {color.shape[:2]} and depth image {depth.shape[:2]} differ")

    depth_norm = _normalize_depth(depth)
    depth_values = depth_norm.reshape(-1)
    depth_smooth = cv2.GaussianBlur(depth_norm, (0, 0), sigmaX=1.25, sigmaY=1.25)

    band_edges = _compute_band_edges(depth_values, layers_requested)
    layer_count = len(band_edges) - 1
    band_bins = band_edges[1:-1]
    labels = np.digitize(depth_smooth, band_bins, right=False).astype(np.int32)

    # Split obvious occluders apart inside each band using a conservative edge mask.
    gray = cv2.cvtColor(color, cv2.COLOR_RGB2GRAY).astype(np.float32) / 255.0
    color_grad_x = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    color_grad_y = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    color_grad = cv2.magnitude(color_grad_x, color_grad_y)
    depth_grad_x = cv2.Sobel(depth_smooth, cv2.CV_32F, 1, 0, ksize=3)
    depth_grad_y = cv2.Sobel(depth_smooth, cv2.CV_32F, 0, 1, ksize=3)
    depth_grad = cv2.magnitude(depth_grad_x, depth_grad_y)
    strong_edges = (depth_grad > np.quantile(depth_grad, 0.88)) | (color_grad > np.quantile(color_grad, 0.92))

    refined_labels = np.zeros_like(labels)
    current_label = 1
    layer_infos = []
    for band_index in range(layer_count):
        band_mask = labels == band_index
        softened_mask = band_mask & ~strong_edges
        num_components, component_labels = cv2.connectedComponents(softened_mask.astype(np.uint8), connectivity=8)

        band_visual_mask = np.zeros_like(band_mask, dtype=bool)
        for component_id in range(1, num_components):
            component_mask = component_labels == component_id
            if np.count_nonzero(component_mask) == 0:
                continue
            component_mask = cv2.dilate(component_mask.astype(np.uint8), np.ones((3, 3), dtype=np.uint8), iterations=1).astype(bool)
            component_mask &= band_mask
            if not np.any(component_mask):
                continue
            refined_labels[component_mask] = current_label
            band_visual_mask |= component_mask
            current_label += 1

        # Anything left in the band still belongs to that layer family.
        leftover_mask = band_mask & ~band_visual_mask
        if np.any(leftover_mask):
            refined_labels[leftover_mask] = current_label
            current_label += 1

        layer_mask = band_mask
        layer_name = ["near", "mid_near", "mid", "mid_far", "far", "sky"][
            min(band_index, 5)
        ]
        mask_path = out_dir / f"layer_{band_index:02d}_{layer_name}.png"
        _save_mask(layer_mask, mask_path)

        components = _rank_components(
            depth_norm=depth_smooth,
            labels=labels,
            band_index=band_index,
            min_region_pixels=min_region_pixels,
            max_components_per_band=max_components_per_band,
            out_dir=out_dir,
        )

        layer_infos.append(
            {
                "index": band_index,
                "name": layer_name,
                "mask_path": mask_path.name,
                "pixel_count": int(np.count_nonzero(layer_mask)),
                "depth_min": round(float(band_edges[band_index]), 6),
                "depth_max": round(float(band_edges[band_index + 1]), 6),
                "median_depth": round(float(np.median(depth_smooth[layer_mask])), 6),
                "candidate_components": components,
            }
        )

    _write_preview(color, labels + 1, out_dir / "layer_preview.png", alpha=0.42)
    _write_preview(color, refined_labels, out_dir / "component_preview.png", alpha=0.50)

    return {
        "width": int(color.shape[1]),
        "height": int(color.shape[0]),
        "layers_requested": layers_requested,
        "layers_generated": layer_count,
        "depth_band_edges": [round(float(edge), 6) for edge in band_edges],
        "notes": [
            "Lower normalized depth values are nearer to the camera.",
            "These masks are suggestions. Expect manual cleanup around silhouettes and hidden-content reveals.",
            "High-scoring candidate components are likely foreground occluders worth separating into their own layers.",
        ],
        "layers": layer_infos,
    }


def _load_mask(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("L"), dtype=np.uint8) >= 128


def _feather_mask(mask: np.ndarray, dilate_iterations: int = 2) -> np.ndarray:
    expanded = cv2.dilate(mask.astype(np.uint8), np.ones((3, 3), dtype=np.uint8), iterations=dilate_iterations)
    feather = cv2.GaussianBlur(expanded.astype(np.float32), (0, 0), sigmaX=1.0, sigmaY=1.0)
    return np.clip(feather * 255.0, 0.0, 255.0).astype(np.uint8)


def _make_rgba_plate(color: np.ndarray, alpha: np.ndarray) -> np.ndarray:
    rgba = np.zeros((color.shape[0], color.shape[1], 4), dtype=np.uint8)
    rgba[:, :, :3] = color
    rgba[:, :, 3] = alpha
    return rgba


def _runtime_factor_from_depth(depth_value: float) -> float:
    return round(float(np.clip(1.20 - depth_value * 0.65, 0.55, 1.20)), 4)


def emit_runtime_assets(
    color: np.ndarray,
    room: LoadedRoom,
    summary: dict,
    out_dir: Path,
    min_component_score: float,
    max_runtime_layers: int,
) -> dict:
    broad_layers = []
    for layer in summary["layers"]:
        broad_layers.append(
            {
                "name": layer["name"],
                "mask_path": out_dir / layer["mask_path"],
                "median_depth": layer["median_depth"],
                "factor": _runtime_factor_from_depth(layer["median_depth"]),
            }
        )

    if not broad_layers:
        raise ValueError("no broad depth layers available for runtime asset generation")

    broad_layers.sort(key=lambda item: item["factor"])
    if len(broad_layers) > max_runtime_layers + 1:
        broad_layers = broad_layers[-(max_runtime_layers + 1):]

    base_layer = broad_layers[0]
    base_alpha = _feather_mask(_load_mask(base_layer["mask_path"]))
    base_rgba = _make_rgba_plate(color, base_alpha)
    base_path = out_dir / "base_plate.png"
    Image.fromarray(base_rgba).save(base_path)

    runtime_layers = []
    for index, layer in enumerate(broad_layers[1:]):
        feather_alpha = _feather_mask(_load_mask(layer["mask_path"]))
        overlay_path = out_dir / f"overlay_{index:02d}.png"
        overlay_rgba = _make_rgba_plate(color, feather_alpha)
        Image.fromarray(overlay_rgba).save(overlay_path)
        runtime_layers.append(
            {
                "name": layer["name"],
                "image": overlay_path.name,
                "factor": layer["factor"],
                "median_depth": round(float(layer["median_depth"]), 6),
            }
        )

    manifest = {
        "format": "grim_parallax_layers_v1",
        "origin": {"x": room.origin_x, "y": room.origin_y},
        "base": {
            "image": base_path.name,
            "factor": base_layer["factor"],
        },
        "layers": runtime_layers,
    }

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


def _find_lab_bitmap_pairs(lab_path: Path) -> list[tuple[str, str]]:
    members = sorted(entry.member_name for entry in _read_lab_entries(lab_path))
    lookup = set(members)
    pairs = []
    for member in members:
        if not member.endswith(".bm"):
            continue
        z_member = member[:-3] + ".zbm"
        if z_member in lookup:
            pairs.append((member, z_member))
    return pairs


def process_room(
    color_spec: str,
    depth_spec: str,
    out_dir: Path,
    layers_requested: int,
    min_region_pixels: int,
    max_components_per_band: int,
    emit_runtime: bool,
    runtime_min_score: float,
    runtime_max_layers: int,
) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    room = load_room_inputs(color_spec, depth_spec)
    result = suggest_layers(
        color=room.color,
        depth=room.depth,
        layers_requested=layers_requested,
        min_region_pixels=min_region_pixels,
        max_components_per_band=max_components_per_band,
        out_dir=out_dir,
    )
    result["inputs"] = {
        "color": color_spec,
        "depth": depth_spec,
    }
    result["origin"] = {
        "x": room.origin_x,
        "y": room.origin_y,
    }

    summary_path = out_dir / "layers.json"
    summary_path.write_text(json.dumps(result, indent=2), encoding="utf-8")

    if emit_runtime:
        emit_runtime_assets(
            color=room.color,
            room=room,
            summary=result,
            out_dir=out_dir,
            min_component_score=runtime_min_score,
            max_runtime_layers=runtime_max_layers,
        )

    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--color", help="Path to the room color image, or LAB member syntax like data000.lab::ha_intha.bm")
    parser.add_argument("--depth", help="Path to the room depth image, or LAB member syntax like data000.lab::ha_intha.zbm")
    parser.add_argument("--output-dir", help="Directory for masks, previews, and JSON summary")
    parser.add_argument("--batch-lab", help="Process every .bm/.zbm pair in a Grim .lab archive")
    parser.add_argument("--output-root", help="Root directory used with --batch-lab; each setup gets its own subdirectory")
    parser.add_argument("--layers", type=int, default=4, help="Number of broad depth bands to suggest")
    parser.add_argument("--min-region-pixels", type=int, default=2048, help="Ignore candidate components smaller than this")
    parser.add_argument("--max-components-per-band", type=int, default=4, help="Keep at most this many occluder candidates per band")
    parser.add_argument("--emit-runtime-assets", action="store_true", help="Also emit engine-ready manifest.json plus PNG plates")
    parser.add_argument("--runtime-min-score", type=float, default=0.14, help="Minimum component score to keep as a runtime overlay plate")
    parser.add_argument("--runtime-max-layers", type=int, default=6, help="Maximum number of runtime overlay plates per room")
    parser.add_argument("--list-lab", help="List archive members in a Grim .lab file and exit")
    parser.add_argument("--grep", default="", help="Optional case-insensitive filter to combine with --list-lab")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list_lab:
        entries = _read_lab_entries(Path(args.list_lab))
        grep = args.grep.lower()
        for entry in entries:
            if grep and grep not in entry.member_name:
                continue
            print(entry.member_name)
        return 0

    if args.batch_lab:
        if not args.output_root:
            raise SystemExit("--output-root is required with --batch-lab")

        lab_path = Path(args.batch_lab)
        output_root = Path(args.output_root)
        pairs = _find_lab_bitmap_pairs(lab_path)
        if not pairs:
            raise SystemExit(f"No .bm/.zbm pairs found in {lab_path}")

        failures: list[tuple[str, str]] = []
        for color_member, depth_member in pairs:
            setup_name = Path(color_member).stem
            out_dir = output_root / setup_name
            try:
                result = process_room(
                    color_spec=f"{lab_path}::{color_member}",
                    depth_spec=f"{lab_path}::{depth_member}",
                    out_dir=out_dir,
                    layers_requested=max(2, args.layers),
                    min_region_pixels=max(64, args.min_region_pixels),
                    max_components_per_band=max(1, args.max_components_per_band),
                    emit_runtime=args.emit_runtime_assets,
                    runtime_min_score=max(0.0, args.runtime_min_score),
                    runtime_max_layers=max(1, args.runtime_max_layers),
                )
            except Exception as exc:
                failures.append((setup_name, str(exc)))
                print(f"[{setup_name}] skipped: {exc}")
                continue

            print(f"[{setup_name}] wrote {out_dir / 'layers.json'}")
            if args.emit_runtime_assets:
                print(f"[{setup_name}] wrote {out_dir / 'manifest.json'}")
            print(f"[{setup_name}] {len(result['layers'])} broad layers, {sum(len(layer['candidate_components']) for layer in result['layers'])} candidate components")

        if failures:
            print(f"Skipped {len(failures)} entries with incompatible inputs.")
        return 0

    if not args.color or not args.depth or not args.output_dir:
        raise SystemExit("--color, --depth, and --output-dir are required unless --list-lab or --batch-lab is used")

    out_dir = Path(args.output_dir)
    result = process_room(
        color_spec=args.color,
        depth_spec=args.depth,
        out_dir=out_dir,
        layers_requested=max(2, args.layers),
        min_region_pixels=max(64, args.min_region_pixels),
        max_components_per_band=max(1, args.max_components_per_band),
        emit_runtime=args.emit_runtime_assets,
        runtime_min_score=max(0.0, args.runtime_min_score),
        runtime_max_layers=max(1, args.runtime_max_layers),
    )

    print(f"Wrote {out_dir / 'layers.json'}")
    print(f"Wrote {out_dir / 'layer_preview.png'}")
    print(f"Wrote {out_dir / 'component_preview.png'}")
    if args.emit_runtime_assets:
        print(f"Wrote {out_dir / 'manifest.json'}")
    for layer in result["layers"]:
        print(f"Layer {layer['index']:02d} {layer['name']}: {layer['mask_path']} ({layer['pixel_count']} px)")
        for component in layer["candidate_components"]:
            print(
                f"  component score={component['score']:.3f} area={component['area']} "
                f"bbox={component['bbox']} mask={component['mask_path']}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
