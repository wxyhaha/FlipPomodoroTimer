"""Generate photorealistic pre-rendered Nixie tube RGB565 sprites (96x220 x 10)."""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFilter

W, H = 96, 220

def sample_cubic(p0, p1, p2, p3, steps=28):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1.0 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    return pts

def sample_arc(cx, cy, rx, ry, deg_start, deg_end, steps=36):
    pts = []
    for i in range(steps + 1):
        a = math.radians(deg_start + (deg_end - deg_start) * i / steps)
        pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return pts

def digit_strokes(d):
    # Tall, slender IN-18 style wire numerals inside tube interior (x: 24..72, y: 50..162)
    if d == 0:
        return [sample_arc(48, 106, 19, 54, 0, 360, 48)]
    if d == 1:
        return [[(36, 66), (49, 52), (49, 160)], [(36, 160), (62, 160)]]
    if d == 2:
        pts = sample_arc(48, 72, 18, 20, -165, 25, 28)
        pts += sample_cubic((65, 79), (59, 106), (32, 134), (29, 159), 26)
        pts += [(67, 159)]
        return [pts]
    if d == 3:
        top = sample_arc(47, 74, 17, 22, -160, 88, 28)
        bot = sample_arc(48, 131, 19, 29, -90, 165, 30)
        return [top + bot]
    if d == 4:
        return [[(56, 160), (56, 52), (28, 124), (68, 124)], [(46, 160), (64, 160)]]
    if d == 5:
        top = [(63, 54), (34, 54), (31, 96)]
        arc = sample_arc(47, 128, 19, 32, -130, 162, 32)
        return [top + arc]
    if d == 6:
        top = sample_cubic((60, 52), (35, 58), (28, 92), (28, 128), 24)
        loop = sample_arc(48, 130, 19, 30, 180, -180, 36)
        return [top + loop]
    if d == 7:
        return [[(30, 62), (30, 54), (66, 54), (40, 160)]]
    if d == 8:
        top = sample_arc(48, 77, 16, 25, 0, 360, 32)
        bot = sample_arc(48, 132, 19, 28, 0, 360, 32)
        return [top, bot]
    if d == 9:
        loop = sample_arc(48, 82, 19, 29, 0, 360, 36)
        tail = sample_cubic((67, 84), (66, 126), (55, 154), (33, 158), 24)
        return [loop, tail]
    return []

def build_tube_base_and_glass():
    base = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    px = base.load()
    cx = W / 2.0
    rx = 34.0
    top_y = 48
    bot_y = 172

    # 1. Glass cylinder dark interior + cylindrical rim shading
    for y in range(20, bot_y + 1):
        for x in range(W):
            dx = (x - cx) / rx
            if y < top_y:
                # Dome top + nipple tip
                dy = (top_y - y) / 28.0
                r2 = dx * dx + dy * dy
                inside_nipple = abs(x - cx) <= 4.5 and 12 <= y <= 24
                if r2 > 1.0 and not inside_nipple:
                    continue
                nx = dx
            else:
                if abs(dx) > 1.0:
                    continue
                nx = dx

            # Subtle warm glass interior gradient + cylindrical edge rim
            edge = abs(nx)
            rim = max(0.0, (edge - 0.72) / 0.28)
            spec_l = math.exp(-((nx + 0.62) / 0.14) ** 2)
            spec_r = math.exp(-((nx - 0.74) / 0.10) ** 2)
            bg_r = int(8 + 26 * rim + 75 * spec_l + 38 * spec_r)
            bg_g = int(5 + 24 * rim + 75 * spec_l + 38 * spec_r)
            bg_b = int(4 + 26 * rim + 82 * spec_l + 42 * spec_r)
            px[x, y] = (min(255, bg_r), min(255, bg_g), min(255, bg_b), 255)

    # 2. Unlit stacked cathode wires inside tube (3, 8, 5, 0)
    unlit = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ud = ImageDraw.Draw(unlit)
    for bg_d in (0, 5, 3, 8):
        for st in digit_strokes(bg_d):
            ud.line(st, fill=(38, 22, 14, 150), width=2)
    # Internal vertical support rods & subtle mesh
    ud.line([(20, 52), (20, 170)], fill=(55, 42, 34, 170), width=2)
    ud.line([(76, 52), (76, 170)], fill=(55, 42, 34, 170), width=2)
    for my in range(56, 168, 7):
        for mx in range(18, 78, 7):
            ud.point((mx + ((my // 7) % 2) * 3, my), fill=(48, 34, 24, 140))
    base = Image.alpha_composite(base, unlit)

    # 3. Ceramic/metal stepped pedestal & pins at bottom (y: 170..216)
    px = base.load()
    for y in range(169, 200):
        # Stepped collar profile
        if y < 176:
            half_w = 34.0
        elif y < 188:
            half_w = 38.0
        else:
            half_w = 33.0
        for x in range(W):
            dx = (x - cx) / half_w
            if abs(dx) > 1.0:
                continue
            # Metallic cylindrical shading
            light = 0.28 + 0.55 * math.exp(-((dx + 0.35) / 0.45) ** 2) + 0.25 * (1.0 - abs(dx))
            if y in (176, 188):
                light *= 0.45
            v = int(min(255, 22 + 95 * light))
            px[x, y] = (v, int(v * 0.94), int(v * 0.90), 255)

    # Bottom pins
    draw = ImageDraw.Draw(base)
    for px_x in (34, 41, 48, 55, 62):
        draw.line([(px_x, 200), (px_x, 214)], fill=(70, 68, 66, 255), width=2)
        draw.line([(px_x - 1, 200), (px_x - 1, 212)], fill=(120, 118, 114, 255), width=1)
    return base

def render_tube_digit(d, base_img):
    # Supersampled 2x crisp filament drawing
    S = 2
    wire = Image.new("L", (W * S, H * S), 0)
    wd = ImageDraw.Draw(wire)
    for st in digit_strokes(d):
        pts = [(x * S, y * S) for x, y in st]
        wd.line(pts, fill=255, width=3 * S, joint="curve")
        for x, y in (pts[0], pts[-1]):
            wd.ellipse((x - 3, y - 3, x + 3, y + 3), fill=255)

    core = wire.resize((W, H), Image.Resampling.LANCZOS)
    core_soft = core.filter(ImageFilter.GaussianBlur(0.8))
    bloom_inner = core.filter(ImageFilter.GaussianBlur(3.2))
    bloom_mid = core.filter(ImageFilter.GaussianBlur(7.5))
    bloom_wide = core.filter(ImageFilter.GaussianBlur(14.0))

    # Composite glow layer with pedestal reflection
    glow_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gp = glow_layer.load()
    cp = core.load()
    csp = core_soft.load()
    bip = bloom_inner.load()
    bmp = bloom_mid.load()
    bwp = bloom_wide.load()

    for y in range(28, 194):
        for x in range(10, W - 10):
            dx = abs(x - W / 2.0) / 34.0
            if dx > 1.0:
                continue
            # Pedestal bounce light at y=166..182
            ped_boost = 1.0 if y < 168 else max(0.0, 1.0 - (y - 168) / 16.0) * 0.65
            w_val = bwp[x, y] * 1.1 + bmp[x, y] * 0.9 + bip[x, y] * 1.1
            hot = max(cp[x, y], int(csp[x, y] * 0.9))
            intensity = min(255.0, (w_val * 0.95 + hot * 0.85) * ped_boost)
            if intensity < 3:
                continue
            if hot > 140 and y < 170:
                t = (hot - 140) / 115.0
                r = 255
                g = int(145 + 100 * t)
                b = int(40 + 165 * t)
                a = 255
            elif intensity > 110:
                t = min(1.0, (intensity - 110) / 145.0)
                r = 255
                g = int(58 + 90 * t)
                b = int(6 + 34 * t)
                a = min(255, int(120 + 135 * t))
            else:
                t = intensity / 110.0
                r = int(235 * t)
                g = int(52 * t)
                b = int(4 * t)
                a = int(160 * t)
            gp[x, y] = (min(255, r), min(255, g), min(255, b), min(255, a))

    comp = Image.alpha_composite(base_img, glow_layer)

    # Foreground subtle glass reflection & honeycomb mesh over the glow
    fg = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    fp = fg.load()
    for y in range(26, 170):
        for x in range(14, W - 14):
            nx = (x - W / 2.0) / 34.0
            if abs(nx) > 0.98:
                continue
            spec = math.exp(-((nx + 0.58) / 0.12) ** 2)
            if spec > 0.15:
                fp[x, y] = (230, 235, 245, int(42 * spec))
            # Delicate front wire mesh
            if ((x + y) % 8 == 0 or (x - y + 256) % 8 == 0) and abs(nx) < 0.88 and y > 48:
                fp[x, y] = (18, 12, 10, 48)

    final_img = Image.alpha_composite(comp, fg).convert("RGB")
    fpx = final_img.load()
    rgb565 = []
    for y in range(H):
        for x in range(W):
            r, g, b = fpx[x, y]
            rgb565.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return rgb565

def main():
    base = build_tube_base_and_glass()
    lines = [
        "// Generated by tools/generate_nixie_tubes.py; photorealistic RGB565 Nixie tubes.",
        "#pragma once",
        "#include <stdint.h>",
        f"static constexpr int NIXIE_TUBE_W = {W};",
        f"static constexpr int NIXIE_TUBE_H = {H};",
        f"static const uint16_t NIXIE_TUBES[10][{W * H}] = {{",
    ]
    for d in range(10):
        vals = render_tube_digit(d, base)
        lines.append("  {")
        for y in range(0, W * H, W):
            lines.append("    " + ",".join(f"0x{v:04X}" for v in vals[y:y + W]) + ",")
        lines.append("  },")
    lines.append("};")
    target = Path(__file__).resolve().parents[1] / "firmware/FlipPomodoroTimer/NixieFont.h"
    target.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"Generated {target} ({W}x{H} RGB565 x 10)")

if __name__ == "__main__":
    main()
