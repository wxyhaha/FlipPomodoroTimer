"""Generate a selenographically accurate 45x45 lunar albedo texture and 8-phase preview."""
from pathlib import Path
import math
import numpy as np
from PIL import Image, ImageDraw

R = 21
SIZE = R * 2 + 3  # 45x45, center at (22, 22)
SUPERSAMPLE = 4


def hash3(ix: np.ndarray, iy: np.ndarray, iz: np.ndarray) -> np.ndarray:
    """Deterministic 3D integer hash returning float in [0, 1)."""
    n = (ix * 374761393 + iy * 668265263 + iz * 2147483647) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    n = n ^ (n >> 16)
    return n.astype(np.float64) / 4294967296.0


def value_noise_3d(x: np.ndarray, y: np.ndarray, z: np.ndarray, seed: int = 42) -> np.ndarray:
    """Smooth trilinear 3D value noise."""
    x = x + seed * 17.13
    y = y + seed * 31.71
    z = z + seed * 47.37
    ix = np.floor(x).astype(np.int64)
    iy = np.floor(y).astype(np.int64)
    iz = np.floor(z).astype(np.int64)
    fx = x - ix
    fy = y - iy
    fz = z - iz
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    uz = fz * fz * (3.0 - 2.0 * fz)

    c000 = hash3(ix, iy, iz)
    c100 = hash3(ix + 1, iy, iz)
    c010 = hash3(ix, iy + 1, iz)
    c110 = hash3(ix + 1, iy + 1, iz)
    c001 = hash3(ix, iy, iz + 1)
    c101 = hash3(ix + 1, iy, iz + 1)
    c011 = hash3(ix, iy + 1, iz + 1)
    c111 = hash3(ix + 1, iy + 1, iz + 1)

    c00 = c000 * (1.0 - ux) + c100 * ux
    c10 = c010 * (1.0 - ux) + c110 * ux
    c01 = c001 * (1.0 - ux) + c101 * ux
    c11 = c011 * (1.0 - ux) + c111 * ux

    c0 = c00 * (1.0 - uy) + c10 * uy
    c1 = c01 * (1.0 - uy) + c11 * uy
    return c0 * (1.0 - uz) + c1 * uz


def fbm3(x: np.ndarray, y: np.ndarray, z: np.ndarray, octaves: int = 4, seed: int = 42) -> np.ndarray:
    """3D fractional Brownian motion in [-1, 1]."""
    val = np.zeros_like(x, dtype=np.float64)
    amp = 0.5
    freq = 1.0
    norm = 0.0
    for i in range(octaves):
        n = value_noise_3d(x * freq, y * freq, z * freq, seed=seed + i * 19)
        val += amp * (n * 2.0 - 1.0)
        norm += amp
        amp *= 0.5
        freq *= 2.07
    return val / norm


def sph_dist_deg(lat: np.ndarray, lon: np.ndarray, clat_deg: float, clon_deg: float,
                 scale_lat: float = 1.0, scale_lon: float = 1.0) -> np.ndarray:
    """Anisotropic spherical distance in degrees."""
    dlat = (lat - math.radians(clat_deg)) / scale_lat
    cos_m = math.cos(math.radians(clat_deg))
    dlon = ((lon - math.radians(clon_deg)) * max(0.35, cos_m)) / scale_lon
    return np.degrees(np.sqrt(dlat * dlat + dlon * dlon))


def smoothstep(edge0: float, edge1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def build_moon_albedo() -> np.ndarray:
    """Compute 45x45 lunar albedo array (uint8, 0 outside disc, 45..255 inside)."""
    ss = SIZE * SUPERSAMPLE
    center = (R + 1) * SUPERSAMPLE + (SUPERSAMPLE - 1) * 0.5
    rf = R * SUPERSAMPLE

    yy, xx = np.mgrid[0:ss, 0:ss].astype(np.float64)
    dx = xx - center
    dy = yy - center
    dist = np.sqrt(dx * dx + dy * dy)
    inside = dist <= rf + 0.5

    nx = np.clip(dx / rf, -1.0, 1.0)
    ny = np.clip(dy / rf, -1.0, 1.0)
    nz = np.sqrt(np.maximum(0.0, 1.0 - nx * nx - ny * ny))

    # Domain warp in 3D sphere space so coastal borders are irregular and foreshorten cleanly
    wx = fbm3(nx * 3.4, ny * 3.4, nz * 3.4, octaves=4, seed=42)
    wy = fbm3(nx * 3.4, ny * 3.4, nz * 3.4, octaves=4, seed=107)
    wz = fbm3(nx * 3.4, ny * 3.4, nz * 3.4, octaves=4, seed=211)

    wnx = nx + 0.095 * wx
    wny = ny + 0.095 * wy
    wnz = np.maximum(0.02, nz + 0.065 * wz)
    norm_w = np.sqrt(wnx * wnx + wny * wny + wnz * wnz)
    wnx /= norm_w
    wny /= norm_w
    wnz /= norm_w

    # Selenographic lat/lon (North up: +lat when ny < 0, East right: +lon when nx > 0)
    lat = np.arcsin(np.clip(-wny, -1.0, 1.0))
    lon = np.arctan2(wnx, wnz)

    # Unwarped lat/lon for sharp bright crater rays & small features
    lat_raw = np.arcsin(np.clip(-ny, -1.0, 1.0))
    lon_raw = np.arctan2(nx, np.maximum(0.02, nz))

    # Base highlands albedo with gentle natural mottling and fine roughness
    mottle = fbm3(nx * 5.5, ny * 5.5, nz * 5.5, octaves=3, seed=303)
    micro = fbm3(nx * 16.0, ny * 16.0, nz * 16.0, octaves=3, seed=409)
    albedo = 0.84 + 0.065 * mottle + 0.035 * micro

    # Brighter rugged southern highlands
    south_mask = smoothstep(-20.0, -48.0, np.degrees(lat_raw))
    albedo += 0.06 * south_mask

    # Major lunar maria: (clat, clon, rad_deg, strength, scale_lat, scale_lon)
    maria_defs = [
        # Oceanus Procellarum (multi-lobe western expanse)
        (32.0, -49.0, 19.0, 0.43, 1.15, 0.95),
        (12.0, -42.0, 19.0, 0.42, 1.10, 0.95),
        (-5.0, -37.0, 15.0, 0.37, 1.00, 0.95),
        (44.0, -48.0, 11.0, 0.33, 0.85, 1.15),
        # Mare Imbrium + Sinus Iridum
        (33.0, -17.0, 20.0, 0.45, 0.95, 1.05),
        (45.0, -31.0, 7.5,  0.36, 0.85, 1.10),
        # Mare Frigoris (elongated northern band)
        (56.0, -18.0, 9.0,  0.34, 0.55, 1.80),
        (57.0,   6.0, 9.0,  0.34, 0.55, 1.80),
        (54.0,  26.0, 8.0,  0.31, 0.60, 1.50),
        # Mare Serenitatis
        (27.0,  18.0, 13.5, 0.43, 0.98, 1.02),
        # Mare Tranquillitatis
        (8.5,   30.5, 15.0, 0.47, 0.92, 1.08),
        # Mare Crisium (isolated NE oval)
        (17.0,  58.5, 10.5, 0.46, 0.90, 1.12),
        # Mare Fecunditatis
        (-7.5,  50.5, 12.5, 0.41, 1.10, 0.92),
        # Mare Nectaris
        (-15.5, 34.5, 9.0,  0.40, 1.00, 1.00),
        # Mare Vaporum / Sinus Medii
        (13.0,   4.0, 7.5,  0.34, 0.90, 1.10),
        (2.0,    1.0, 6.0,  0.30, 0.90, 1.10),
        # Mare Nubium & Mare Cognitum
        (-21.0, -16.0, 13.5, 0.38, 1.05, 1.00),
        (-10.5, -23.0, 8.5,  0.36, 1.00, 1.00),
        # Mare Humorum
        (-24.5, -39.0, 9.0,  0.41, 1.00, 1.00),
        # Mare Australe / eastern limb hints
        (-36.0,  62.0, 9.0,  0.28, 1.00, 1.00),
    ]

    # Combine maria coverage smoothly so overlapping basins don't go pitch black
    mare_field = np.zeros_like(albedo)
    for clat, clon, rad, strength, slat, slon in maria_defs:
        d_deg = sph_dist_deg(lat, lon, clat, clon, slat, slon)
        cov = 1.0 - smoothstep(rad * 0.55, rad * 1.18, d_deg)
        contrib = strength * cov
        # Smooth union: 1 - (1 - a)*(1 - b) scaled gently
        mare_field = np.maximum(mare_field, contrib) + 0.16 * np.minimum(mare_field, contrib)

    # Interior basalt texture inside maria
    mare_tex = 0.045 * fbm3(nx * 10.0, ny * 10.0, nz * 10.0, octaves=3, seed=521)
    albedo = albedo * (1.0 - np.clip(mare_field, 0.0, 0.54)) + mare_tex * np.clip(mare_field * 2.0, 0.0, 1.0)

    # Montes Apenninus & Caucasus highland ridge separating Imbrium and Serenitatis
    d_ap = sph_dist_deg(lat_raw, lon_raw, 19.5, -2.5, 1.35, 0.55)
    albedo += 0.14 * (1.0 - smoothstep(2.0, 7.5, d_ap))
    d_cau = sph_dist_deg(lat_raw, lon_raw, 36.0, 7.5, 1.25, 0.55)
    albedo += 0.10 * (1.0 - smoothstep(2.0, 6.5, d_cau))

    # Bright rayed craters: Tycho, Copernicus, Kepler, Aristarchus
    d_tycho = sph_dist_deg(lat_raw, lon_raw, -43.3, -11.2)
    albedo += 0.19 * (1.0 - smoothstep(1.5, 5.2, d_tycho))
    # Subtle Tycho radial ray streaks across southern highlands
    ang_tycho = np.arctan2(lat_raw - math.radians(-43.3), lon_raw - math.radians(-11.2))
    rays = np.power(np.maximum(0.0, np.cos(ang_tycho * 7.0 + 0.4)), 4.0)
    rays *= (1.0 - smoothstep(4.0, 28.0, d_tycho)) * smoothstep(2.5, 7.0, d_tycho)
    albedo += 0.065 * rays

    for clat, clon, boost, rmax in [
        (9.7, -20.1, 0.15, 5.8),   # Copernicus
        (8.1, -38.0, 0.10, 4.2),   # Kepler
        (23.7, -47.4, 0.13, 4.0),  # Aristarchus
    ]:
        dc = sph_dist_deg(lat_raw, lon_raw, clat, clon)
        albedo += boost * (1.0 - smoothstep(1.2, rmax, dc))

    albedo = np.clip(albedo, 0.18, 1.0)
    # Zero outside disc, quantize inside to [45, 255]
    val = np.where(inside, np.round(45.0 + 210.0 * ((albedo - 0.18) / 0.82)), 0.0)
    val = np.clip(val, 0.0, 255.0)

    # Box downsample 4x4 -> 45x45 (averaging only inside-disc coverage, or full cell for clean edge)
    blocks = val.reshape(SIZE, SUPERSAMPLE, SIZE, SUPERSAMPLE)
    inside_blocks = inside.reshape(SIZE, SUPERSAMPLE, SIZE, SUPERSAMPLE)
    cov_count = inside_blocks.sum(axis=(1, 3))
    sum_val = (blocks * inside_blocks).sum(axis=(1, 3))

    out = np.zeros((SIZE, SIZE), dtype=np.uint8)
    mask = cov_count > 0
    out[mask] = np.clip(np.round(sum_val[mask] / cov_count[mask]), 45, 255).astype(np.uint8)
    return out


def rgb565_quantize(r: float, g: float, b: float) -> tuple[int, int, int]:
    r5 = max(0, min(31, int(r) >> 3))
    g6 = max(0, min(63, int(g) >> 2))
    b5 = max(0, min(31, int(b) >> 3))
    return (r5 << 3, g6 << 2, b5 << 3)


def render_preview(tex: np.ndarray, out_path: Path) -> None:
    """Render 8 lunar phases using the exact C++ shader math & RGB565 quantization."""
    phases = [0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875]
    labels = ["0.00 New", "0.125 WaxCres", "0.25 FirstQ", "0.375 WaxGib",
              "0.50 Full", "0.625 WanGib", "0.75 LastQ", "0.875 WanCres"]
    tile = 136
    cols, rows = 4, 2
    img = Image.new("RGB", (cols * tile, rows * tile), (10, 18, 36))
    draw = ImageDraw.Draw(img)

    for idx_p, (phase, label) in enumerate(zip(phases, labels)):
        tx = (idx_p % cols) * tile
        ty = (idx_p // cols) * tile
        cx, cy = tx + tile // 2, ty + tile // 2 - 4

        theta = phase * 2.0 * math.pi
        sx = math.sin(theta)
        sz = -math.cos(theta)
        illum_frac = 0.5 * (1.0 - math.cos(theta))
        glow_scale = 0.12 + 0.88 * (max(0.0, min(1.0, illum_frac)) ** 0.85)

        r_glow = 56
        r_glow2 = r_glow * r_glow
        r_in2 = 32 * 32

        # 1. Background + glow
        px_buf = img.load()
        for y in range(max(ty, cy - r_glow), min(ty + tile, cy + r_glow + 1)):
            dy = y - cy
            dy2 = dy * dy
            for x in range(max(tx, cx - r_glow), min(tx + tile, cx + r_glow + 1)):
                dx = x - cx
                d2 = dx * dx + dy2
                if d2 >= r_glow2:
                    continue
                bg = px_buf[x, y]
                u_out = 1.0 - d2 / r_glow2
                oa = int((u_out ** 4) * 255.0 * 0.24 * glow_scale)
                if oa > 1:
                    bg = (
                        (bg[0] * (255 - oa) + 125 * oa) // 255,
                        (bg[1] * (255 - oa) + 168 * oa) // 255,
                        (bg[2] * (255 - oa) + 232 * oa) // 255,
                    )
                if d2 < r_in2:
                    u_in = 1.0 - d2 / r_in2
                    ia = int((u_in ** 4) * 255.0 * 0.22 * glow_scale)
                    if ia > 1:
                        bg = (
                            (bg[0] * (255 - ia) + 220 * ia) // 255,
                            (bg[1] * (255 - ia) + 232 * ia) // 255,
                            (bg[2] * (255 - ia) + 250 * ia) // 255,
                        )
                px_buf[x, y] = bg

        # 2. Lunar disc sprite
        sx0 = cx - (R + 1)
        sy0 = cy - (R + 1)
        rf = float(R)
        for py in range(SIZE):
            dy = float(py - (R + 1))
            for px in range(SIZE):
                dx = float(px - (R + 1))
                d = math.hypot(dx, dy)
                if d > rf + 1.0:
                    continue
                aa = 1.0 if d <= rf - 0.35 else max(0.0, min(1.0, 1.0 - (d - (rf - 0.35)) / 1.35))
                nx = max(-1.0, min(1.0, dx / rf))
                ny = max(-1.0, min(1.0, dy / rf))
                nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))

                raw_alb = tex[py, px] / 255.0
                if raw_alb < 0.15:
                    raw_alb = 0.65
                albedo = max(0.32, min(1.0, 0.32 + 0.68 * raw_alb))

                ndotl = nx * sx + nz * sz
                t_lit = max(0.0, min(1.0, (ndotl + 0.025) / 0.085))
                lit = t_lit * t_lit * (3.0 - 2.0 * t_lit)

                mu0 = max(0.0, ndotl)
                mu = max(0.08, nz)
                ls = min(1.06, (2.0 * mu0) / (mu0 + mu)) if mu0 > 0.0 else 0.0
                shade = 0.86 * ls + 0.14 * mu0

                lit_val = max(0.0, min(1.0, albedo * shade))
                lit_col = (
                    112 + (248 - 112) * lit_val,
                    120 + (246 - 120) * lit_val,
                    134 + (238 - 134) * lit_val,
                )
                es_mod = 0.55 + 0.45 * albedo
                earth_col = (36 * es_mod, 48 * es_mod, 74 * es_mod)
                dark_alpha = 18.0 + 34.0 * (1.0 - illum_frac)

                c = (
                    earth_col[0] + (lit_col[0] - earth_col[0]) * lit,
                    earth_col[1] + (lit_col[1] - earth_col[1]) * lit,
                    earth_col[2] + (lit_col[2] - earth_col[2]) * lit,
                )
                qc = rgb565_quantize(*c)
                a = int(aa * (dark_alpha + (255.0 - dark_alpha) * lit))
                if a > 1:
                    bg = px_buf[sx0 + px, sy0 + py]
                    px_buf[sx0 + px, sy0 + py] = (
                        (bg[0] * (255 - a) + qc[0] * a) // 255,
                        (bg[1] * (255 - a) + qc[1] * a) // 255,
                        (bg[2] * (255 - a) + qc[2] * a) // 255,
                    )

        draw.text((tx + 12, ty + tile - 18), label, fill=(195, 210, 235))

    scaled = img.resize((img.width * 2, img.height * 2), Image.Resampling.NEAREST)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    scaled.save(out_path)


def write_header(tex: np.ndarray, header_path: Path) -> None:
    lines = [
        "// Generated lunar nearside albedo texture; tools/generate_moon_texture.py.",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        f"static constexpr int kMoonTexRadius = {R};",
        f"static constexpr int kMoonTexSize = {SIZE};",
        "",
        f"static const uint8_t kMoonAlbedo[{SIZE} * {SIZE}] PROGMEM = {{",
    ]
    for row in tex:
        lines.append("  " + ",".join(f"{int(v):3d}" for v in row) + ",")
    lines.append("};")
    header_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    tex = build_moon_albedo()
    header_path = root / "firmware" / "FlipPomodoroTimer" / "MoonTexture.h"
    write_header(tex, header_path)
    preview_path = Path(
        r"C:\Users\wxy\.gemini\antigravity\brain\af22a690-4a7e-4d91-9012-250ffa1f7b91\scratch\moon_phases_preview.png"
    )
    render_preview(tex, preview_path)
    print(f"Wrote {header_path} ({tex.size} bytes) and {preview_path}")


if __name__ == "__main__":
    main()
