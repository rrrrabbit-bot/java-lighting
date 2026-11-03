#!/usr/bin/env python3
"""Generate Java-look reference assets and the Vibrant Visuals resource pack.

This is the *reference* implementation of Java Edition's lightmap maths for the
JavaLighting project.  It is written straight from the decompiled Java sources
documented in docs/research/java-lighting.md and is used to

  * validate the C++ port in src/lighting/JavaLightmap.cpp against known pixels,
  * render comparison images for the docs,
  * emit the resource pack JSON (lighting / shadows / local lights / pbr).

Run with the bundled Python:

    <python.exe> tools/gen_reference.py
"""

from __future__ import annotations

import json
import math
import uuid
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
PACK = ROOT / "resource_pack"
DOCS_IMG = ROOT / "docs" / "img"

SIZE = 16
PACK_UUID = "5c1f3a52-7d2b-4a91-9f01-0d7c2b6e8a44"
MODULE_UUID = "b1a0c9d4-3e57-4f28-8c6a-5d1e9f0b7a23"


# --------------------------------------------------------------------------- #
# Java Edition lightmap port (MC 1.21.4 lightmap.fsh; same maths as 1.20.1
# LightTexture.updateLightTexture with a doubled 0.04 grey mix).
# --------------------------------------------------------------------------- #

def clamp01(x: float) -> float:
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def not_gamma(x: float) -> float:
    """Java: 1 - (1 - x)^4  (LightTexture#notGamma)."""
    g = 1.0 - clamp01(x)
    return 1.0 - g * g * g * g


def brightness(level: int, dimension_ambient: float = 0.0) -> float:
    """Java: LightTexture.getBrightness(DimensionType, int).

    ``Mth.lerp(ambientLight, curved, 1.0)`` == curved + ambient * (1 - curved).
    """
    f = level / 15.0
    curved = f / (4.0 - 3.0 * f)
    return curved + dimension_ambient * (1.0 - curved)


def lightmap_pixel(block_light: int, sky_light: int, *, sky_darken: float,
                   gamma: float = 0.5, dimension_ambient: float = 0.0,
                   darken_world: float = 0.0, night_vision: float = 0.0,
                   legacy_double_mix: bool = False) -> tuple[float, float, float]:
    """One lightmap cell, matching Java's per-pixel body."""
    sky_darken = clamp01(sky_darken)
    sky_factor = sky_darken * 0.95 + 0.05
    block_factor = 1.5  # blockLightRedFlicker + 1.5

    q = brightness(sky_light, dimension_ambient) * sky_factor
    r = brightness(block_light, dimension_ambient) * block_factor

    # Warm block-light ramp: R > G > B for every r < 1.
    cr = r
    cg = r * ((r * 0.6 + 0.4) * 0.6 + 0.4)
    cb = r * (r * r * 0.6 + 0.4)

    # Sky-light colour: neutral at noon, blue at night.
    s = sky_darken * 0.65 + 0.35
    sky_color = (s, s, 1.0)

    c = [cr + sky_color[0] * q, cg + sky_color[1] * q, cb + sky_color[2] * q]
    c = [x + (0.75 - x) * 0.04 for x in c]

    if darken_world > 0.0:
        c = [x + (x * m - x) * darken_world for x, m in zip(c, (0.7, 0.6, 0.6))]

    if night_vision > 0.0:
        mx = max(c)
        if mx < 1.0 and mx > 0.0:
            c = [x + (x / mx - x) * night_vision for x in c]

    c = [clamp01(x) for x in c]

    if legacy_double_mix:  # MC 1.20.1 keeps an extra mix before notGamma's result
        c = [clamp01(x) for x in c]

    ng = [not_gamma(x) for x in c]
    c = [x + (n - x) * max(0.0, gamma) for x, n in zip(c, ng)]

    c = [x + (0.75 - x) * 0.04 for x in c]
    return tuple(clamp01(x) for x in c)


def build_table(**kwargs) -> list[list[tuple[float, float, float]]]:
    """[skyLight][blockLight] table, as Java writes it (x=block, y=sky)."""
    table = []
    for sky in range(SIZE):
        row = []
        for block in range(SIZE):
            row.append(lightmap_pixel(block, sky, **kwargs))
        table.append(row)
    return table


def to_u8(c: tuple[float, float, float]) -> tuple[int, int, int]:
    # Java does (int)(component * 255), i.e. truncation.
    return tuple(int(clamp01(x) * 255.0) for x in c)  # type: ignore[return-value]


def table_u8(**kwargs) -> list[list[tuple[int, int, int]]]:
    return [[to_u8(c) for c in row] for row in build_table(**kwargs)]


def checksum(table_u8) -> int:
    h = 0xCBF29CE484222325
    for row in table_u8:
        for rgb in row:
            for v in rgb:
                h ^= v
                h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# --------------------------------------------------------------------------- #
# Validation: pixels pinned by docs/research/java-lighting.md (MC 1.20.1).
# --------------------------------------------------------------------------- #

LEGACY_DAY = dict(sky_darken=1.0, legacy_double_mix=True)
LEGACY_NIGHT = dict(sky_darken=0.2, legacy_double_mix=True)


def hex_of(rgb) -> str:
    return "#%02X%02X%02X" % rgb


def validate() -> list[str]:
    problems = []

    def expect(cond, msg):
        if not cond:
            problems.append(msg)

    day = table_u8(**LEGACY_DAY)
    night = table_u8(**LEGACY_NIGHT)

    # Values transcribed from the reference table in docs/research/java-lighting.md
    checks = [
        (day[0][0], (0x19, 0x19, 0x19), "day sky0/block0"),
        (day[0][15], (0xFC, 0xFC, 0xFC), "day sky0/block15"),
        (day[15][0], (0xFB, 0xFB, 0xFB), "day sky15/block0"),
        (day[15][15], (0xFC, 0xFC, 0xFC), "day sky15/block15"),
        (night[15][0], (0x50, 0x50, 0x7D), "night sky15/block0"),
        (night[0][0], (0x19, 0x19, 0x19), "night sky0/block0"),
    ]
    for got, want, name in checks:
        expect(got == want, f"{name}: got {hex_of(got)} expected {hex_of(want)}")

    # Gamma sweep at (block0, sky0). Java truncates (int)(x*255), so allow +/-1
    # against the rounded values published in the research doc.
    for gamma, want_high, want_low in ((0.0, (0xFC, 0xFC, 0xFC), (0x0F, 0x0F, 0x0F)),
                                       (0.5, (0xFC, 0xFC, 0xFC), (0x19, 0x19, 0x19)),
                                       (1.0, (0xFC, 0xFC, 0xFC), (0x24, 0x24, 0x24))):
        t = table_u8(sky_darken=1.0, gamma=gamma, legacy_double_mix=True)
        expect(t[0][15] == want_high, f"gamma {gamma} block15: {hex_of(t[0][15])}")
        expect(
            all(abs(a - b) <= 1 for a, b in zip(t[0][0], want_low)),
            f"gamma {gamma} block0: {hex_of(t[0][0])} expected {hex_of(want_low)} +/-1",
        )

    # Night sky light must be blue, day sky light neutral.
    expect(night[15][0][2] > night[15][0][0] + 30, "night sky light must be blue")
    expect(abs(day[15][0][0] - day[15][0][2]) <= 2, "day sky light must be neutral")
    return problems


# --------------------------------------------------------------------------- #
# Pack generation
# --------------------------------------------------------------------------- #

JAVA_LOOK = json.loads((ROOT / "tools" / "java_look.json").read_text(encoding="utf-8"))


def java_torch_color() -> str:
    """RGB tint of Java's block-light ramp at a representative light level.

    Java has no per-light colours: every emitter uses the same warm curve
    ``(r, r*(0.36r+0.64), r*(0.6r^2+0.4))``.  We sample it where most of the
    torch's visible falloff lives and normalise so V is unchanged.
    """
    r = JAVA_LOOK["blockLightSample"]
    c = (r, r * ((r * 0.6 + 0.4) * 0.6 + 0.4), r * (r * r * 0.6 + 0.4))
    m = max(c)
    return "#%02X%02X%02X" % tuple(round(x / m * 255) for x in c)


def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def build_pack() -> None:
    PACK.mkdir(parents=True, exist_ok=True)

    write_json(PACK / "manifest.json", {
        "format_version": 2,
        "header": {
            "name": "JavaLighting — Java Edition look",
            "description": "Vibrant Visuals preset that moves Bedrock lighting/shadow towards Java Edition.",
            "uuid": PACK_UUID,
            "version": [0, 1, 0],
            "min_engine_version": [1, 21, 120],
        },
        "modules": [{
            "type": "resources",
            "uuid": MODULE_UUID,
            "version": [0, 1, 0],
        }],
        "capabilities": ["pbr"],
    })

    look = JAVA_LOOK
    write_json(PACK / "shadows" / "global.json", {
        "format_version": "1.21.80",
        "minecraft:shadow_settings": {
            "shadow_style": look["shadowStyle"],
            "texel_size": look["texelSize"],
        },
    })

    write_json(PACK / "lighting" / "global.json", {
        "format_version": "1.26.0",
        "minecraft:lighting_settings": {
            "description": {"identifier": "javalighting:java"},
            "directional_lights": {
                "orbital": {
                    "sun": {
                        "illuminance": look["sunIlluminance"],
                        "color": look["sunColor"],
                    },
                    "moon": {
                        "illuminance": look["moonIlluminance"],
                        "color": look["moonColor"],
                    },
                    "orbital_offset_degrees": 0.0,
                },
                "flash": {"illuminance": 5.0, "color": "#FFFFFF"},
            },
            "emissive": {"desaturation": 0.1},
            "ambient": {
                "illuminance": look["ambientIlluminance"],
                "color": look["ambientColor"],
            },
            "sky": {"intensity": look["skyIntensity"]},
        },
    })

    torch = java_torch_color()
    local = {"minecraft:torch": {"light_color": torch, "light_type": "static_light"}}
    for block in look["staticLightBlocks"]:
        local[f"minecraft:{block}"] = {"light_color": torch, "light_type": "static_light"}
    write_json(PACK / "local_lighting" / "local_lighting.json", {
        "format_version": "1.21.120",
        "minecraft:local_light_settings": local,
    })

    # Java Edition has no PBR: everything is matte diffuse. MERS =
    # (metalness, emissive, roughness, subsurface) -> [0, 0, 255, 0].
    mers = [0, 0, 255, 0]
    write_json(PACK / "pbr" / "global.json", {
        "format_version": "1.21.40",
        "minecraft:pbr_fallback_settings": {
            kind: {"global_metalness_emissive_roughness_subsurface": mers}
            for kind in ("blocks", "actors", "particles", "items")
        },
    })

    make_icon()
    make_lightmap_images()


def make_icon() -> None:
    img = Image.new("RGBA", (128, 128), (24, 28, 36, 255))
    d = ImageDraw.Draw(img)
    # Warm torch gradient over a cool sky gradient: Java's signature contrast.
    for y in range(128):
        for x in range(128):
            k = math.hypot(x - 40, y - 88) / 110.0
            warm = max(0.0, 1.0 - k)
            cool = max(0.0, 1.0 - math.hypot(x - 92, y - 40) / 120.0)
            r = int(min(255, 40 + 210 * warm + 30 * cool))
            g = int(min(255, 36 + 140 * warm + 60 * cool))
            b = int(min(255, 44 + 60 * warm + 170 * cool))
            img.putpixel((x, y), (r, g, b, 255))
    d.rectangle([8, 8, 119, 119], outline=(255, 255, 255, 90), width=2)
    PACK.joinpath("pack_icon.png").parent.mkdir(parents=True, exist_ok=True)
    img.save(PACK / "pack_icon.png")


def make_lightmap_images() -> None:
    DOCS_IMG.mkdir(parents=True, exist_ok=True)
    for name, kwargs, label in (
        ("lightmap_java_day", dict(sky_darken=1.0, legacy_double_mix=True), "Java day (skyDarken=1.0)"),
        ("lightmap_java_night", dict(sky_darken=0.2, legacy_double_mix=True), "Java night (skyDarken=0.2)"),
        ("lightmap_java_dusk", dict(sky_darken=0.6, legacy_double_mix=True), "Java dusk (skyDarken=0.6)"),
    ):
        table = table_u8(**kwargs)
        scale = 24
        margin = 26
        img = Image.new("RGB", (SIZE * scale + 2 * margin, SIZE * scale + 2 * margin), (20, 20, 24))
        d = ImageDraw.Draw(img)
        for y in range(SIZE):
            for x in range(SIZE):
                img.paste(
                    Image.new("RGB", (scale, scale), table[y][x]),
                    (margin + x * scale, margin + (SIZE - 1 - y) * scale),
                )
        d.text((margin, 8), f"{label}  X=blockLight 0-15, Y=skyLight 0-15", fill=(220, 220, 220))
        img.save(DOCS_IMG / f"{name}.png")


def dump_reference() -> None:
    DOCS_IMG.mkdir(parents=True, exist_ok=True)
    out = {
        "java_version": "1.21.4 (lightmap.fsh), 1.20.1 legacy variant",
        "size": SIZE,
        "gamma": 0.5,
        "day": table_u8(sky_darken=1.0),
        "night": table_u8(sky_darken=0.2),
        "day_checksum_fnv1a": checksum(table_u8(sky_darken=1.0)),
        "night_checksum_fnv1a": checksum(table_u8(sky_darken=0.2)),
    }
    (ROOT / "tests").mkdir(exist_ok=True)
    (ROOT / "tests" / "expected_lightmap.json").write_text(
        json.dumps(out, indent=2) + "\n", encoding="utf-8"
    )


def main() -> int:
    problems = validate()
    if problems:
        print("VALIDATION FAILED:")
        for p in problems:
            print("  -", p)
        return 1

    build_pack()
    dump_reference()

    day = table_u8(**LEGACY_DAY)
    night = table_u8(**LEGACY_NIGHT)
    print("Java lightmap reference OK")
    print("  day   (block15,sky0) =", hex_of(day[0][15]), " (block0,sky15) =", hex_of(day[15][0]))
    print("  night (block0,sky15) =", hex_of(night[15][0]))
    print("  day checksum  = 0x%016X" % checksum(day))
    print("  night checksum= 0x%016X" % checksum(night))
    print("Pack written to", PACK)
    print("Images written to", DOCS_IMG)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
