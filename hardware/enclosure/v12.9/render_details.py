"""Draw a documentation detail sheet for the V12.9 enclosure.

The diagrams explain nominal dimensions and assembly relationships. The STL
files remain the manufacturing source of truth.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


OUT = Path(__file__).parent / "details.png"
SCALE = 2
WIDTH, HEIGHT = 1800, 1260
BG = "#edf2f5"
INK = "#263844"
MUTED = "#60727c"
SHELL = "#5598a0"
SHELL_DARK = "#39747c"
PANEL = "#364653"
SCREEN = "#101c27"
BATTERY = "#477dca"
RETAINER = "#e7a44d"
LID = "#9985aa"
CYAN = "#139fa5"
WHITE = "#ffffff"

FONT_CANDIDATES = [
    Path("C:/Windows/Fonts/msyh.ttc"),
    Path("C:/Windows/Fonts/simhei.ttf"),
    Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
]
FONT_PATH = next((path for path in FONT_CANDIDATES if path.exists()), None)
if FONT_PATH is None:
    raise RuntimeError("A Chinese TrueType font is required to render the detail sheet")

image = Image.new("RGB", (WIDTH * SCALE, HEIGHT * SCALE), BG)
draw = ImageDraw.Draw(image)


def xy(value):
    if isinstance(value, tuple):
        return tuple(round(part * SCALE) for part in value)
    return round(value * SCALE)


def font(size):
    return ImageFont.truetype(str(FONT_PATH), round(size * SCALE))


def text(pos, value, size=18, color=INK):
    draw.text(xy(pos), value, font=font(size), fill=color)


def line(points, color=INK, width=2):
    draw.line([xy(point) for point in points], fill=color, width=max(1, round(width * SCALE)), joint="curve")


def rect(box, fill, outline=None, width=2, radius=0):
    coords = xy(box)
    if radius:
        draw.rounded_rectangle(coords, radius=round(radius * SCALE), fill=fill, outline=outline,
                               width=max(1, round(width * SCALE)))
    else:
        draw.rectangle(coords, fill=fill, outline=outline, width=max(1, round(width * SCALE)))


def ellipse(box, fill, outline=None, width=2):
    draw.ellipse(xy(box), fill=fill, outline=outline, width=max(1, round(width * SCALE)))


def arrow(start, end, color=CYAN, width=3, head=10):
    line([start, end], color, width)
    import math
    angle = math.atan2(end[1] - start[1], end[0] - start[0])
    left = (end[0] - head * math.cos(angle - 0.48), end[1] - head * math.sin(angle - 0.48))
    right = (end[0] - head * math.cos(angle + 0.48), end[1] - head * math.sin(angle + 0.48))
    draw.polygon([xy(end), xy(left), xy(right)], fill=color)


def double_arrow(start, end, label, label_pos, color=MUTED):
    line([start, end], color, 2)
    arrow((start[0] + (4 if end[0] >= start[0] else -4), start[1]), start, color, 2, 7)
    arrow((end[0] - (4 if end[0] >= start[0] else -4), end[1]), end, color, 2, 7)
    text(label_pos, label, 15, color)


def card(box, heading, subtitle=None):
    rect(box, WHITE, "#d6e0e5", 1, 18)
    text((box[0] + 24, box[1] + 16), heading, 21, INK)
    if subtitle:
        text((box[0] + 24, box[1] + 47), subtitle, 14, MUTED)


# Header
text((58, 30), "V12.9 · 外壳结构细节图", 31, INK)
text((60, 82), "尺寸单位 mm；局部剖面用于说明装配关系，正式打印以三件 STL 为准。", 17, MUTED)

# Three outside views
card((45, 130, 600, 505), "外观三视图", "主壳圆角 R2；面板不另开按键和 USB 孔")
card((630, 130, 1170, 505), "右侧视图", "面板从右侧滑入；右侧未设退出锁，仍需首件检查")
card((1195, 130, 1755, 505), "后盖视图", "独立后盖，四角螺丝固定；中央为通气槽")

# Front view: 90.5 x 92.5 mm outer profile, 78 mm front mouth.
rect((205, 222, 449, 472), SHELL, SHELL_DARK, 3, 12)
rect((222, 239, 432, 455), SCREEN, "#1b2b36", 2, 7)
rect((228, 245, 426, 449), None, "#53616a", 1, 6)
double_arrow((205, 210), (449, 210), "90.5", (302, 188))
line([(192, 222), (180, 222)], MUTED, 2)
line([(192, 472), (180, 472)], MUTED, 2)
double_arrow((180, 222), (180, 472), "92.5", (132, 338))
text((145, 480), "前口 78 × 78", 15, MUTED)
text((145, 503), "最窄内口 72.8 × 72.8", 15, MUTED)

# Right-side profile, with the panel sitting 3 mm behind the front plane.
rect((718, 220, 1036, 490), SHELL, SHELL_DARK, 3, 10)
rect((728, 232, 777, 478), PANEL, None, 0, 2)
rect((728, 232, 734, 478), SCREEN, None, 0, 1)
rect((1036, 220, 1050, 490), LID, "#756487", 2, 3)
line([(728, 208), (728, 220)], CYAN, 2)
line([(738, 208), (738, 220)], CYAN, 2)
double_arrow((728, 207), (738, 207), "内缩 3", (739, 197))
double_arrow((718, 504), (1050, 504), "总深 94.5", (832, 509))
text((716, 535), "显示面板厚 14.5；后盖外板厚 4", 15, MUTED)

# Rear view: lid, four screw seats, ventilation slots.
rect((1322, 214, 1610, 500), LID, "#756487", 3, 10)
for cx in (1350, 1582):
    for cy in (242, 472):
        ellipse((cx - 7, cy - 7, cx + 7, cy + 7), "#51465e", "#d6cedd", 2)
for y in (327, 340, 353, 366, 379):
    rect((1438, y, 1494, y + 4), "#62566f", None, 0, 2)
text((1328, 514), "90.5 × 92.5；四颗后盖螺丝", 15, MUTED)

# Lower technical details
card((45, 555, 875, 1215), "A / 面板侧插与导向结构", "正视结构示意；中央留空避让喇叭，面板由右向左插入")
card((900, 555, 1755, 1215), "B / 电池压条前舌固定", "侧剖结构示意；压条前舌穿过挡边，后端由两颗螺丝固定")

# Panel fitting view.
rect((105, 665, 530, 1090), "#d9e7e9", SHELL_DARK, 3, 16)
rect((123, 683, 512, 1072), PANEL, "#647782", 2, 10)
rect((153, 713, 482, 1042), SCREEN, None, 0, 6)
# Two separate left-side stops and upper/lower rear guide lips.
rect((120, 705, 148, 758), SHELL_DARK, None, 0, 5)
rect((120, 997, 148, 1050), SHELL_DARK, None, 0, 5)
rect((145, 684, 492, 699), CYAN, None, 0, 5)
rect((145, 1056, 492, 1071), CYAN, None, 0, 5)
arrow((495, 864), (411, 864), RETAINER, 4, 14)
text((215, 1096), "左上、左下止挡各覆盖 4 mm；中间开放避让喇叭", 15, MUTED)
text((120, 1122), "上下后导轨各搭边 2 mm · 槽高 86.90 mm · 前后入口深 14.90 mm", 14, MUTED)
text((120, 1147), "面板上下、前后间隙各 0.2 mm；右侧目前没有防退出锁。", 14, MUTED)

# Battery retainer cross section, drawn schematically in Y-Z.
# Front wall is 2 mm thick; tongue spans 3 mm and protrudes 1 mm outside it.
base_x, base_y, px_per_mm = 1320, 1080, 5.0
def yz(y, z):
    return (base_x + (y - 24) * px_per_mm, base_y - z * px_per_mm)

# Tray / front rail / battery reference envelope / retainer arms.
wall_a, wall_b = yz(28.2, 0)[0], yz(30.2, 0)[0]
rect((wall_a, yz(30.2, 8)[1], wall_b, yz(30.2, 0)[1]), SHELL_DARK, None, 0, 2)
batt_left, batt_right = yz(33, 0)[0], yz(70.3, 0)[0]
batt_top, batt_bottom = yz(33, 21.5)[1], yz(33, 1.5)[1]
rect((batt_left, batt_top, batt_right, batt_bottom), BATTERY, "#315a96", 2, 6)
# Retainer top arm, vertical leg and inward-going front tongue.
arm_a, arm_b = yz(30.2, 0)[0], yz(82.5, 0)[0]
rect((arm_a, yz(30.2, 24.5)[1], arm_b, yz(30.2, 22)[1]), RETAINER, "#bd7c2c", 2, 4)
post_a, post_b = yz(30.2, 0)[0], yz(31.7, 0)[0]
rect((post_a, yz(30.2, 24.5)[1], post_b, yz(30.2, 5.2)[1]), RETAINER, "#bd7c2c", 2, 3)
tongue_a, tongue_b = yz(27.2, 0)[0], yz(30.3, 0)[0]
rect((tongue_a, yz(27.2, 6.4)[1], tongue_b, yz(27.2, 5.2)[1]), RETAINER, "#bd7c2c", 2, 2)
# Through-slot is indicated by its outline and centerline through the rail.
rect((wall_a - 2, yz(28.2, 6.6)[1], wall_b + 2, yz(28.2, 4.2)[1]), None, CYAN, 2, 1)
# Rear screw is symbolic; second one lies on the other strap arm.
ellipse((yz(83.5, 18)[0] - 7, yz(83.5, 18)[1] - 7, yz(83.5, 18)[0] + 7, yz(83.5, 18)[1] + 7), "#51465e", WHITE, 1)

# Dimension/callout labels.
text((930, 760), "自由舌长约 3 mm", 17, INK)
text((930, 795), "穿过 2 mm 挡边通孔，舌尖外露 1 mm", 15, MUTED)
text((930, 830), "电池包络：66.7 × 37.3 × 20 mm", 15, MUTED)
text((930, 865), "压条到电池顶部名义间隙 0.5 mm", 15, MUTED)
text((930, 900), "后端两颗螺丝固定；拆装需移开面板和后盖", 15, MUTED)
text((930, 935), "颜色：青绿=挡边 · 橙色=压条 · 蓝色=电池", 14, MUTED)

# Footer and output.
text((60, 1220), "提示：此图为结构说明示意，不替代 STL 尺寸，也不代表打印公差或实物配合已验证。", 15, MUTED)
image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).save(OUT)
print(f"Wrote {OUT}")
