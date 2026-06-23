#!/usr/bin/env python3
"""Bake an HDR equirectangular image into an embedded GGX cubemap mip chain.

The output layout is exactly what raylib's rlLoadTextureCubemap() expects:
for every mip level, the six faces (+X, -X, +Y, -Y, +Z, -Z), stored as
linear RGB16F texels.  Mip 0 is the unfiltered environment; later mips are
GGX importance-sampled convolutions with roughness = mip / (mip_count - 1).

Setup (once):  py -m pip install OpenEXR numpy
Run:           py tools/bake_studio_light.py "<path-to-forest.exr>" src/studio_light_data.c
"""

import argparse
from pathlib import Path

import numpy as np


CUBEMAP_SIZE = 64
MIP_COUNT = 7
SAMPLE_COUNT = 2048


def load_exr_rgb(path: Path) -> np.ndarray:
    import OpenEXR

    exr = OpenEXR.File(str(path))
    channels = exr.channels()
    if "RGB" in channels:
        rgb = np.asarray(channels["RGB"].pixels, dtype=np.float32)
    else:
        rgb = np.stack(
            [np.asarray(channels[name].pixels, dtype=np.float32) for name in "RGB"],
            axis=-1,
        )
    rgb = np.ascontiguousarray(rgb.reshape(rgb.shape[0], rgb.shape[1], 3))
    # Light transport is non-negative.  Clamping malformed negative EXR samples
    # also guarantees that the baked map cannot reintroduce SH-like black lobes.
    return np.maximum(rgb, 0.0)


def face_directions(face: int, size: int) -> np.ndarray:
    """Return OpenGL cubemap texel-center directions for one face."""
    s = (2.0 * (np.arange(size, dtype=np.float32) + 0.5) / size) - 1.0
    u, v = np.meshgrid(s, s)
    one = np.ones_like(u)
    if face == 0:       # +X: sc=-z, tc=-y
        direction = np.stack((one, -v, -u), axis=-1)
    elif face == 1:     # -X: sc= z, tc=-y
        direction = np.stack((-one, -v, u), axis=-1)
    elif face == 2:     # +Y: sc= x, tc= z
        direction = np.stack((u, one, v), axis=-1)
    elif face == 3:     # -Y: sc= x, tc=-z
        direction = np.stack((u, -one, -v), axis=-1)
    elif face == 4:     # +Z: sc= x, tc=-y
        direction = np.stack((u, -v, one), axis=-1)
    else:               # -Z: sc=-x, tc=-y
        direction = np.stack((-u, -v, -one), axis=-1)
    return direction / np.linalg.norm(direction, axis=-1, keepdims=True)


def sample_equirect_bilinear(image: np.ndarray, directions: np.ndarray) -> np.ndarray:
    """Sample a y-up equirectangular HDR image with horizontal wrap."""
    height, width, _ = image.shape
    direction = directions / np.linalg.norm(directions, axis=-1, keepdims=True)
    longitude = np.arctan2(direction[..., 2], direction[..., 0])
    latitude = np.arccos(np.clip(direction[..., 1], -1.0, 1.0))
    x = (longitude / (2.0 * np.pi) + 0.5) * width - 0.5
    y = latitude / np.pi * height - 0.5

    x0_raw = np.floor(x).astype(np.int64)
    y0_raw = np.floor(y).astype(np.int64)
    tx = (x - x0_raw)[..., None]
    ty = (y - y0_raw)[..., None]
    x0 = x0_raw % width
    x1 = (x0_raw + 1) % width
    y0 = np.clip(y0_raw, 0, height - 1)
    y1 = np.clip(y0_raw + 1, 0, height - 1)

    top = image[y0, x0] * (1.0 - tx) + image[y0, x1] * tx
    bottom = image[y1, x0] * (1.0 - tx) + image[y1, x1] * tx
    return top * (1.0 - ty) + bottom * ty


def radical_inverse_vdc(bits: np.ndarray) -> np.ndarray:
    bits = bits.astype(np.uint32)
    bits = ((bits << 16) | (bits >> 16)) & np.uint32(0xFFFFFFFF)
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)
    return bits.astype(np.float64) * 2.3283064365386963e-10


def hammersley(count: int) -> np.ndarray:
    indices = np.arange(count, dtype=np.uint32)
    return np.stack((indices.astype(np.float64) / count, radical_inverse_vdc(indices)), axis=-1)


def prefilter_direction(image: np.ndarray, normal: np.ndarray, roughness: float,
                        samples: np.ndarray) -> np.ndarray:
    """Karis/UE4 split-sum environment convolution for V=N."""
    alpha = roughness * roughness
    phi = 2.0 * np.pi * samples[:, 0]
    cos_theta = np.sqrt(
        (1.0 - samples[:, 1]) /
        np.maximum(1.0 + (alpha * alpha - 1.0) * samples[:, 1], 1e-12)
    )
    sin_theta = np.sqrt(np.maximum(1.0 - cos_theta * cos_theta, 0.0))
    halfway_tangent = np.stack(
        (np.cos(phi) * sin_theta, np.sin(phi) * sin_theta, cos_theta), axis=-1
    )

    up = np.array((0.0, 0.0, 1.0)) if abs(normal[2]) < 0.999 else np.array((1.0, 0.0, 0.0))
    tangent = np.cross(up, normal)
    tangent /= np.linalg.norm(tangent)
    bitangent = np.cross(normal, tangent)
    halfway = (
        tangent[None, :] * halfway_tangent[:, 0:1]
        + bitangent[None, :] * halfway_tangent[:, 1:2]
        + normal[None, :] * halfway_tangent[:, 2:3]
    )
    # In the split-sum prefilter pass V is set to the output direction N.
    light = 2.0 * np.sum(normal[None, :] * halfway, axis=-1, keepdims=True) * halfway - normal
    light /= np.linalg.norm(light, axis=-1, keepdims=True)
    n_dot_l = np.maximum(light @ normal, 0.0)
    valid = n_dot_l > 0.0
    if not np.any(valid):
        return np.zeros(3, dtype=np.float32)
    radiance = sample_equirect_bilinear(image, light[valid])
    weights = n_dot_l[valid]
    return np.sum(radiance * weights[:, None], axis=0) / np.sum(weights)


def bake(image: np.ndarray) -> list[np.ndarray]:
    samples = hammersley(SAMPLE_COUNT)
    mipmaps = []
    for mip in range(MIP_COUNT):
        size = max(1, CUBEMAP_SIZE >> mip)
        roughness = mip / (MIP_COUNT - 1)
        faces = np.empty((6, size, size, 3), dtype=np.float32)
        for face in range(6):
            directions = face_directions(face, size)
            if mip == 0:
                rgb = sample_equirect_bilinear(image, directions)
            else:
                rgb = np.empty((size, size, 3), dtype=np.float32)
                for y in range(size):
                    for x in range(size):
                        rgb[y, x] = prefilter_direction(
                            image, directions[y, x].astype(np.float64), roughness, samples
                        )
            faces[face] = np.maximum(rgb, 0.0)
        mipmaps.append(faces)
        print(f"  mip {mip}: {size}x{size}x6, roughness={roughness:.3f}")
    return mipmaps


def emit(out_path: Path, mipmaps: list[np.ndarray]) -> None:
    # Store the IEEE-754 half bits verbatim so the C compiler does no floating
    # conversion and rlLoadTextureCubemap can upload the array directly.
    values = np.concatenate([mip.reshape(-1, 3) for mip in mipmaps], axis=0)
    values = np.nan_to_num(values, nan=0.0, posinf=65504.0, neginf=0.0)
    bits = np.clip(values, 0.0, 65504.0).astype(np.float16).view(np.uint16).reshape(-1)

    with out_path.open("w", newline="\n") as output:
        output.write(
            "/* Studio light baked from Blender's forest.exr world HDRI.\n"
            " * Original forest light: Poly Haven / Greg Zaal, CC0.\n"
            " *\n"
            " * 64x64x6 RGB16F OpenGL cubemap with 7 mip levels. Mip 0 is the\n"
            " * original radiance; mip N is GGX importance-sampled at roughness\n"
            " * N/6 for Karis/UE4 split-sum PBR IBL. Layout is mip-major, then\n"
            " * faces +X, -X, +Y, -Y, +Z, -Z.\n"
            " *\n"
            " * Generated by tools/bake_studio_light.py - do not edit by hand. */\n\n"
            "#include <stdint.h>\n\n"
            "const uint16_t studio_prefiltered_cubemap[] = {\n"
        )
        for start in range(0, bits.size, 12):
            row = ", ".join(f"0x{value:04x}" for value in bits[start:start + 12])
            output.write(f"    {row},\n")
        output.write("};\n")
    print(f"Wrote {out_path} ({bits.size // 3} RGB16F texels, {bits.nbytes} data bytes)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exr", type=Path, help="source equirectangular OpenEXR")
    parser.add_argument("output", type=Path, help="generated studio_light_data.c")
    args = parser.parse_args()

    image = load_exr_rgb(args.exr)
    print(f"Loaded EXR {image.shape[1]}x{image.shape[0]} from {args.exr}")
    mipmaps = bake(image)
    emit(args.output, mipmaps)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
