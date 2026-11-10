#!/usr/bin/env python3
"""Generate the authored GSP scene for the MiFi simulator and device."""

import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "components" / "app_mifi" / "ui" / "main.json"
ASSET_DIR = OUT.parent / "assets"

BG = "#07111D"
PANEL = "#101C2A"
PANEL_2 = "#142233"
TEXT = "#F4F7FB"
MUTED = "#98A5B5"
CYAN = "#29B9F4"
BLUE = "#168FC9"
GREEN = "#3FD274"
AMBER = "#F1AD45"
LINE = "#223145"
FONT = "NotoSansSC-Regular.otf"
BOLD = FONT
# ESP32-S31 soft-AP accepts these personal modes. WEP, WAPI and OWE are rejected.
AUTH_MODES = (
    "Open System",
    "WPA2-Personal",
    "WPA3-Personal",
    "WPA/WPA2-Personal",
    "WPA2/WPA3-Personal",
)
SSID_ALPHABET = (
    " !\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~"
)

objects = []
I18N = []
UI_CHARSET = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    " .,:;!?·%/-_()↑●✓‹›•"
    "中国移动联通电信广电漫游注册网络良好未连接正在重拨实时下载上传本月流量"
    "设备在线续航约台天小时热点开启关闭分享重新拨号首页蜂窝系统设置密码扫码"
    "连接名称安全频段最大访客手机平板优先限速黑名单上网时间管理暂停所有"
    "客厅书房电脑手表摄像头语言中英文小区自动数据卡就绪正常屏幕亮度息屏"
    "秒分钟永不诊断全部检测模组已互联网公网最近掉线无更新最新版本应用检查"
    "今天期间暂时中断恢复运行日志导出告警获取地址加入信号较弱内容隐藏"
    "关于型号固件时长标识开源许可隐私说明不限速优先"
    "°'刚刚DNS公网版本上次检查期间会并导出告警获取地址加入较弱隐藏"
    "密码名称型号时长开源产品使用以下软件完整文本随源码发布包提供"
    "配置流量统计仅保存本机不会记录信息前脱敏且由用户主动触发"
    "只分析上传清除应用但保留身份注册联网小区管理没有"
    "打开后会出现这里随身、，。网卡连通等待请接入子板完成偏高充放电外供电"
    "插拔移除识别启动完成次事件流实时显示最新条系统状态与动作[]点击放大"
)

ICON_PATHS = {
    "home": '<path d="M3 11.5 12 4l9 7.5V21h-6v-6H9v6H3z"/>',
    "wifi": (
        '<path d="M3 9.5a14 14 0 0 1 18 0"/>'
        '<path d="M6.5 13a9 9 0 0 1 11 0"/>'
        '<path d="M10 16.5a3.5 3.5 0 0 1 4 0"/>'
        '<circle cx="12" cy="20" r="1.2" fill="#fff" stroke="none"/>'
    ),
    "device": (
        '<rect x="3" y="4" width="18" height="13" rx="2"/>'
        '<path d="M8 21h8M12 17v4"/>'
    ),
    "signal": (
        '<rect x="3" y="16" width="3" height="5" rx="1" fill="#fff" stroke="none"/>'
        '<rect x="8" y="12" width="3" height="9" rx="1" fill="#fff" stroke="none"/>'
        '<rect x="13" y="8" width="3" height="13" rx="1" fill="#fff" stroke="none"/>'
        '<rect x="18" y="3" width="3" height="18" rx="1" fill="#fff" stroke="none"/>'
    ),
    "gear": (
        '<path fill="#fff" stroke="none" d="M19.43 12.98c.04-.32.07-.65.07-.98'
        's-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46'
        'c-.12-.22-.37-.31-.6-.22l-2.49 1a7.3 7.3 0 0 0-1.69-.98'
        'l-.38-2.65A.49.49 0 0 0 14 2h-4a.49.49 0 0 0-.49.42'
        'l-.38 2.65c-.61.25-1.18.58-1.69.98l-2.49-1a.5.5 0 0 0-.6.22'
        'l-2 3.46a.5.5 0 0 0 .12.64l2.11 1.65a7.8 7.8 0 0 0 0 1.96'
        'l-2.11 1.65a.5.5 0 0 0-.12.64l2 3.46c.12.22.37.31.6.22'
        'l2.49-1c.51.4 1.08.73 1.69.98l.38 2.65c.03.24.24.42.49.42'
        'h4c.25 0 .47-.18.5-.42l.38-2.65c.61-.25 1.17-.58 1.69-.98'
        'l2.49 1c.23.09.48 0 .6-.22l2-3.46a.5.5 0 0 0-.12-.64z'
        'M12 15.5a3.5 3.5 0 1 1 0-7 3.5 3.5 0 0 1 0 7z"/>'
    ),
    "phone": (
        '<rect x="7" y="2" width="10" height="20" rx="2"/>'
        '<path d="M11 5h2"/>'
        '<circle cx="12" cy="18.5" r="1" fill="#fff" stroke="none"/>'
    ),
    "laptop": (
        '<rect x="4" y="4" width="16" height="11" rx="1.5"/>'
        '<path d="M2 18h20"/>'
    ),
    "tablet": (
        '<rect x="6" y="2" width="12" height="20" rx="2"/>'
        '<circle cx="12" cy="18.5" r="1" fill="#fff" stroke="none"/>'
    ),
    "shield": (
        '<path d="M12 3 5 6v6c0 4 3 6.5 7 8 4-1.5 7-4 7-8V6z"/>'
        '<path d="m9 12 2 2 4-4"/>'
    ),
    "clock": (
        '<circle cx="12" cy="12" r="8"/>'
        '<path d="M12 8v5l3 2"/>'
    ),
    "pause": (
        '<rect x="7" y="5" width="3" height="14" rx="1" fill="#fff" stroke="none"/>'
        '<rect x="14" y="5" width="3" height="14" rx="1" fill="#fff" stroke="none"/>'
    ),
    "battery": (
        '<rect x="3" y="5" width="17" height="14" rx="2"/>'
        '<path d="M20 10h2v4h-2"/>'
        '<rect x="6" y="8" width="10" height="8" rx="1" fill="#fff" stroke="none"/>'
    ),
    "bolt": (
        '<path fill="#fff" stroke="none" d="M13 2 5 13h6l-1 9 9-12h-6z"/>'
    ),
    "qr": (
        '<path d="M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3z'
        'M14 14h3v3h-3zM18 14h3v7h-3zM14 19h3v2h-3z"/>'
    ),
    "refresh": (
        '<path d="M20 7v5h-5M4 17v-5h5"/>'
        '<path d="M18.5 10A7 7 0 0 0 6 7L4 9M5.5 14A7 7 0 0 0 18 17l2-2"/>'
    ),
    "thermo": (
        '<rect x="10" y="2.5" width="4" height="12" rx="2"/>'
        '<circle cx="12" cy="17.5" r="3.2"/>'
    ),
    "power": (
        '<path d="M12 3v6"/>'
        '<path d="M7.2 6.4a7 7 0 1 0 9.6 0"/>'
    ),
    "sun": (
        '<circle cx="12" cy="12" r="3.4"/>'
        '<path d="M12 2.5v2.2M12 19.3v2.2M2.5 12h2.2M19.3 12h2.2'
        'M5.1 5.1l1.6 1.6M17.3 17.3l1.6 1.6M18.9 5.1l-1.6 1.6'
        'M6.7 17.3l-1.6 1.6"/>'
    ),
    "wrench": (
        '<path d="M14.8 6.2a3.6 3.6 0 0 0-4.8 4.8L4 17.2 6.8 20'
        'l6.2-6a3.6 3.6 0 0 0 4.8-4.8L15.2 11.8l-3-3z"/>'
    ),
    "upload": (
        '<path d="M14 4h6v6"/>'
        '<path d="M20 4 10 14"/>'
        '<rect x="4" y="9" width="11" height="11" rx="1.5"/>'
    ),
    "logs": (
        '<path d="M7 3.5h8.5A2 2 0 0 1 17.5 5.5V19H9a2 2 0 0 0-2 2V5.5'
        'A2 2 0 0 1 9 3.5z"/>'
        '<path d="M7 19.5A2 2 0 0 0 9 21.5h10"/>'
        '<path d="M10 8h5M10 12h5"/>'
    ),
    "info": (
        '<circle cx="12" cy="12" r="8"/>'
        '<path d="M12 11v5"/>'
        '<circle cx="12" cy="8" r="0.9" fill="#fff" stroke="none"/>'
    ),
    "eye": (
        '<path d="M2 12s4-6.5 10-6.5S22 12 22 12s-4 6.5-10 6.5S2 12 2 12z"/>'
        '<circle cx="12" cy="12" r="2.4"/>'
    ),
    "chevron": '<path d="M9 5.5 15.5 12 9 18.5"/>',
    "caret": '<path d="M6 9.5 12 16.5 18 9.5"/>',
    "check": '<path d="M5.5 12.5 10 17 18.5 7.5"/>',
    "alert": (
        '<circle cx="12" cy="12" r="8"/>'
        '<path d="M12 7.5v5"/>'
        '<circle cx="12" cy="16.2" r="0.9" fill="#fff" stroke="none"/>'
    ),
    "antenna": (
        '<path d="M12 20v-5"/>'
        '<path d="M8 20h8"/>'
        '<circle cx="12" cy="11" r="1.7"/>'
        '<path d="M8.2 8.6a5.2 5.2 0 0 1 7.6 0"/>'
        '<path d="M5.6 6a8.4 8.4 0 0 1 12.8 0"/>'
    ),
    "link": (
        '<rect x="3" y="4" width="13" height="10" rx="1.5"/>'
        '<path d="M7 18h6M10 14v4"/>'
        '<circle cx="17.2" cy="16.2" r="3.6"/>'
        '<path d="M17.2 14.6v1.8l1.1.7"/>'
    ),
    "list": (
        '<rect x="6" y="3" width="12" height="18" rx="2"/>'
        '<path d="M9 8h6M9 12h6M9 16h4"/>'
    ),
    "globe": (
        '<circle cx="12" cy="12" r="8"/>'
        '<path d="M4 12h16"/>'
        '<path d="M12 4c2.4 2.6 2.4 13.4 0 16M12 4c-2.4 2.6-2.4 13.4 0 16"/>'
    ),
}


def write_svg_assets():
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    for name, body in ICON_PATHS.items():
        svg = (
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'
            '<g id="root" fill="none" stroke="#fff" stroke-width="1.8" '
            'stroke-linecap="round" stroke-linejoin="round">'
            f'{body}</g></svg>'
        )
        (ASSET_DIR / f"{name}.svg").write_text(svg, encoding="utf-8")

    for frame in range(6):
        points = []
        for i in range(19):
            x = 24 + i * 24
            phase = i * 0.42 + frame * 0.55
            y = 218 + math.sin(phase) * 14 + math.sin(phase * 0.47) * 6
            points.append((x, y))
        path = [f"M {points[0][0]:.1f} {points[0][1]:.1f}"]
        for i in range(len(points) - 1):
            p0 = points[max(0, i - 1)]
            p1 = points[i]
            p2 = points[i + 1]
            p3 = points[min(len(points) - 1, i + 2)]
            c1 = (p1[0] + (p2[0] - p0[0]) / 6,
                  p1[1] + (p2[1] - p0[1]) / 6)
            c2 = (p2[0] - (p3[0] - p1[0]) / 6,
                  p2[1] - (p3[1] - p1[1]) / 6)
            path.append(
                f"C {c1[0]:.1f} {c1[1]:.1f} {c2[0]:.1f} {c2[1]:.1f} "
                f"{p2[0]:.1f} {p2[1]:.1f}"
            )
        curve = " ".join(path)
        svg = (
            '<svg xmlns="http://www.w3.org/2000/svg" width="480" height="480" '
            'viewBox="0 0 480 480">'
            '<g id="root">'
            f'<path d="{curve}" fill="none" stroke="#0B78A8" '
            'stroke-width="7" stroke-linecap="round" opacity=".24"/>'
            f'<path d="{curve}" fill="none" stroke="#29B9F4" '
            'stroke-width="2" stroke-linecap="round"/>'
            '</g></svg>'
        )
        (ASSET_DIR / f"wave_{frame}.svg").write_text(svg, encoding="utf-8")


def encode_qoi_rgba(pixels, width, height):
    """Standard QOI, 4 channels. GSP decodes this without ROM tinfl."""
    out = bytearray(b"qoif")
    out += width.to_bytes(4, "big")
    out += height.to_bytes(4, "big")
    out += bytes((4, 0))
    index = [(0, 0, 0, 0)] * 64
    prev = (0, 0, 0, 255)
    run = 0

    def emit_run():
        nonlocal run
        while run:
            chunk = 62 if run > 62 else run
            out.append(0xC0 | (chunk - 1))
            run -= chunk

    for i in range(width * height):
        px = tuple(pixels[i * 4:(i + 1) * 4])
        if px == prev:
            run += 1
            if run == 62:
                emit_run()
            continue
        if run:
            emit_run()
        slot = (px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) % 64
        if index[slot] == px:
            out.append(slot)
        else:
            index[slot] = px
            if px[3] == prev[3]:
                dr = (px[0] - prev[0]) & 0xFF
                dg = (px[1] - prev[1]) & 0xFF
                db = (px[2] - prev[2]) & 0xFF
                dr = dr - 256 if dr > 127 else dr
                dg = dg - 256 if dg > 127 else dg
                db = db - 256 if db > 127 else db
                if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    out.append(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2))
                elif -32 <= dg <= 31 and -8 <= dr - dg <= 7 and -8 <= db - dg <= 7:
                    out.append(0x80 | (dg + 32))
                    out.append(((dr - dg + 8) << 4) | (db - dg + 8))
                else:
                    out.append(0xFE)
                    out += bytes(px[:3])
            else:
                out.append(0xFF)
                out += bytes(px)
        prev = px
    if run:
        emit_run()
    out += b"\x00" * 7 + b"\x01"
    return bytes(out)


def write_device_icons():
    from PIL import Image, ImageDraw

    silver = (176, 190, 206, 255)

    def save(name, paint):
        scale = 4
        canvas = Image.new("RGBA", (38 * scale, 38 * scale), (0, 0, 0, 0))
        paint(ImageDraw.Draw(canvas), scale)
        image = canvas.resize((38, 38), Image.LANCZOS)
        path = ASSET_DIR / f"icon_{name}.png"
        image.save(path)
        return encode_qoi_rgba(image.tobytes(), 38, 38)

    def phone(draw, s):
        draw.rounded_rectangle((9 * s, 2 * s, 29 * s, 36 * s),
                               radius=4 * s, outline=silver, width=2 * s)
        draw.rounded_rectangle((15 * s, 6 * s, 23 * s, 7.6 * s),
                               radius=s, fill=silver)
        draw.rounded_rectangle((16 * s, 31 * s, 22 * s, 32.5 * s),
                               radius=s, fill=silver)

    def laptop(draw, s):
        draw.rounded_rectangle((5 * s, 7 * s, 33 * s, 26 * s),
                               radius=2 * s, outline=silver, width=2 * s)
        draw.rounded_rectangle((2 * s, 29 * s, 36 * s, 31.5 * s),
                               radius=s, fill=silver)

    def tablet(draw, s):
        draw.rounded_rectangle((7 * s, 3 * s, 31 * s, 35 * s),
                               radius=4 * s, outline=silver, width=2 * s)
        draw.ellipse((17.5 * s, 29.5 * s, 20.5 * s, 32.5 * s), fill=silver)

    def watch(draw, s):
        draw.rounded_rectangle((14 * s, 2 * s, 24 * s, 8 * s),
                               radius=s, outline=silver, width=2 * s)
        draw.rounded_rectangle((14 * s, 30 * s, 24 * s, 36 * s),
                               radius=s, outline=silver, width=2 * s)
        draw.rounded_rectangle((8 * s, 8 * s, 30 * s, 30 * s),
                               radius=6 * s, outline=silver, width=2 * s)

    def camera(draw, s):
        draw.rounded_rectangle((4 * s, 11 * s, 34 * s, 32 * s),
                               radius=4 * s, outline=silver, width=2 * s)
        draw.rounded_rectangle((13 * s, 6 * s, 25 * s, 12 * s),
                               radius=2 * s, outline=silver, width=2 * s)
        draw.ellipse((14 * s, 16 * s, 24 * s, 26 * s),
                     outline=silver, width=2 * s)

    blobs = {
        "phone": save("phone", phone),
        "laptop": save("laptop", laptop),
        "tablet": save("tablet", tablet),
        "watch": save("watch", watch),
        "camera": save("camera", camera),
    }
    lines = [
        "/* Generated by tools/generate_ui.py. */",
        "/* 38x38 RGBA QOI. Runtime PNG hits ROM tinfl BAD_PARAM (5814 is not a power of two). */",
    ]
    for name, blob in blobs.items():
        body = ", ".join(str(byte) for byte in blob)
        lines.append(f"static const uint8_t icon_{name}_qoi[] = {{{body}}};")
    (ROOT / "components" / "app_mifi" / "device_icons.inc").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )
    write_default_qr_png(ASSET_DIR / "qr_surface.png",
                         "WIFI:T:WPA;S:Mosaico_4G;P:mifi2026;;", 92, 3)
    write_default_qr_png(ASSET_DIR / "qr_large_surface.png",
                         "WIFI:T:WPA;S:Mosaico_4G;P:mifi2026;;", 200, 6)
    write_default_speed_chart_png(ASSET_DIR / "speed_chart.png")


def write_default_speed_chart_png(out_path):
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (432, 32), (7, 17, 29))
    draw = ImageDraw.Draw(img)
    # Flat baseline at y=28 when speed is 0.0 Mbps
    draw.line((0, 28, 431, 28), fill=(18, 60, 88), width=2)
    draw.line((0, 28, 431, 28), fill=(41, 185, 244), width=1)
    img.save(out_path)


def write_default_qr_png(out_path, payload, side=92, scale=3):
    from PIL import Image

    def gf_mul(x, y):
        z = 0
        for i in range(7, -1, -1):
            z = ((z << 1) ^ ((z >> 7) * 0x1D)) & 0xFF
            if (y >> i) & 1:
                z ^= x
        return z

    raw = payload.encode("utf-8")[:53]
    size = 29
    data_cw, ecc_cw = 55, 15
    bits = [0, 1, 0, 0] + [(len(raw) >> i) & 1 for i in range(7, -1, -1)]
    for b in raw:
        bits.extend((b >> i) & 1 for i in range(7, -1, -1))
    bits.extend([0] * min(4, data_cw * 8 - len(bits)))
    while len(bits) % 8:
        bits.append(0)
    data = [int("".join(str(x) for x in bits[i:i + 8]), 2)
            for i in range(0, len(bits), 8)]
    pad = 0xEC
    while len(data) < data_cw:
        data.append(pad)
        pad ^= (0xEC ^ 0x11)
    coeff = [0] * (ecc_cw - 1) + [1]
    root = 1
    for _ in range(ecc_cw):
        for j in range(ecc_cw):
            coeff[j] = gf_mul(coeff[j], root)
            if j + 1 < ecc_cw:
                coeff[j] ^= coeff[j + 1]
        root = gf_mul(root, 2)
    rem = [0] * ecc_cw
    for b in data:
        factor = b ^ rem[0]
        rem = rem[1:] + [0]
        for j in range(ecc_cw):
            rem[j] ^= gf_mul(coeff[j], factor)
    codewords = data + rem

    modules = [[0] * size for _ in range(size)]
    is_func = [[False] * size for _ in range(size)]

    def set_f(x, y, dark):
        if 0 <= x < size and 0 <= y < size:
            modules[y][x] = 1 if dark else 0
            is_func[y][x] = True

    for i in range(size):
        set_f(6, i, i % 2 == 0)
        set_f(i, 6, i % 2 == 0)
    for cx, cy in ((3, 3), (size - 4, 3), (3, size - 4)):
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                set_f(cx + dx, cy + dy, max(abs(dx), abs(dy)) not in (2, 4))
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            set_f(size - 7 + dx, size - 7 + dy, max(abs(dx), abs(dy)) != 1)

    def draw_fmt():
        val = (1 << 3) | 0
        r = val
        for _ in range(10):
            r = (r << 1) ^ ((r >> 9) * 0x537)
        fbits = ((val << 10) | r) ^ 0x5412
        for i in range(6):
            set_f(8, i, bool((fbits >> i) & 1))
        set_f(8, 7, bool((fbits >> 6) & 1))
        set_f(8, 8, bool((fbits >> 7) & 1))
        set_f(7, 8, bool((fbits >> 8) & 1))
        for i in range(9, 15):
            set_f(14 - i, 8, bool((fbits >> i) & 1))
        for i in range(8):
            set_f(size - 1 - i, 8, bool((fbits >> i) & 1))
        for i in range(8, 15):
            set_f(8, size - 15 + i, bool((fbits >> i) & 1))
        set_f(8, size - 8, True)

    draw_fmt()
    ibit = 0
    right = size - 1
    while right >= 1:
        if right == 6:
            right = 5
        for vert in range(size):
            for j in range(2):
                x = right - j
                upward = ((right + 1) & 2) == 0
                y = (size - 1 - vert) if upward else vert
                if not is_func[y][x] and ibit < len(codewords) * 8:
                    modules[y][x] = (codewords[ibit >> 3] >> (7 - (ibit & 7))) & 1
                    ibit += 1
        right -= 2
    for y in range(size):
        for x in range(size):
            if not is_func[y][x] and (x + y) % 2 == 0:
                modules[y][x] ^= 1
    draw_fmt()

    img = Image.new("RGB", (side, side), (255, 255, 255))
    px = img.load()
    margin = (side - size * scale) // 2
    for y in range(size * scale):
        for x in range(size * scale):
            if modules[y // scale][x // scale]:
                px[margin + x, margin + y] = (7, 17, 29)
    img.save(out_path)


def add(kind, parent=-1, **props):
    obj = {"type": kind, **props, "parent": parent}
    objects.append(obj)
    return len(objects) - 1


def layer(name, visible=False):
    return add(
        "layer", x=0, y=0, w=480, h=480, name=name,
        bind=f"{name}_visible", bind_target="visible", hidden=not visible
    )


def box(parent, x, y, w, h, color=PANEL, radius=14, **props):
    return add(
        "container", parent, x=x, y=y, w=w, h=h,
        bg_color=color, radius=radius, **props
    )


def raster(parent, asset, x, y, w, h, **props):
    data = {
        "image": f"assets/{asset}.png",
        "x": x, "y": y, "w": w, "h": h,
        "fit": "contain",
    }
    data.update(props)
    return add("image", parent, **data)


def image(parent, asset, x, y, w, h, tint=None, scene_svg=True, **props):
    data = {
        "image": f"assets/{asset}.svg",
        "x": x, "y": y, "w": w, "h": h,
        "fit": "stretch" if scene_svg else "contain",
    }
    if scene_svg:
        data["svg_layout"] = "canvas"
        data["svg_element"] = "root"
    if tint:
        data["tint"] = tint
    data.update(props)
    return add("image", parent, **data)


def label(parent, text, x, y, w, h, size=16, color=TEXT, align="left",
          bold=False, name=None, bind=None, charset=None, **props):
    data = {
        "text": text, "x": x, "y": y, "w": w, "h": h,
        "font_size": size, "fg_color": color, "text_align": align,
        "font": BOLD if bold else FONT,
    }
    if name:
        data["name"] = name
    if bind:
        data["bind"] = bind
    if charset:
        data["font_charset"] = charset
    data.update(props)
    return add("label", parent, **data)


def button(parent, text, x, y, w, h, action, color=PANEL_2,
           fg=TEXT, radius=13, name=None, bind=None, **props):
    data = {
        "text": text, "x": x, "y": y, "w": w, "h": h,
        "font_size": 15, "fg_color": fg, "bg_color": color,
        "radius": radius, "font": FONT,
        "events": [{"event": "click", "action": "call", "target_name": action}],
    }
    if name:
        data["name"] = name
    if bind:
        data["bind"] = bind
        data.setdefault("font_charset", text_charset(text))
    data.update(props)
    return add("button", parent, **data)


def text_charset(*parts):
    chars = set(" 0123456789")
    for part in parts:
        chars.update(part)
    return "".join(sorted(chars))


def tlabel(parent, key, zh, en, x, y, w, h, size=16, color=TEXT,
           align="left", bold=False, charset=None, **props):
    I18N.append((key, zh, en))
    return label(
        parent, zh, x, y, w, h, size, color, align, bold,
        name=key, bind=key,
        charset=text_charset(zh, en) if charset is None else charset, **props
    )


def tbutton(parent, key, zh, en, x, y, w, h, action, color=PANEL_2,
            fg=TEXT, radius=13, charset=None, **props):
    I18N.append((key, zh, en))
    return button(
        parent, zh, x, y, w, h, action, color, fg, radius,
        name=key, bind=key,
        font_charset=text_charset(zh, en) if charset is None else charset,
        **props
    )


def page_background(parent):
    box(parent, 0, 0, 480, 480, BG, 0)


CARRIER_CHARSET = text_charset(
    "中国联通 4G", "China Unicom 4G",
    "中国移动 4G", "China Mobile 4G",
    "中国电信 4G", "China Telecom 4G",
    "无 SIM 卡", "No SIM card",
    "未插 SIM 卡", "SIM not inserted",
    "未接入 4G 子板", "No 4G module",
    "正在识别 4G...", "Init 4G...",
)


def header(parent, title=None, key=None, en=None):
    image(parent, "signal", 30, 14, 20, 20, CYAN)
    carrier_key = "carrier" if title is None else f"{key}_carrier"
    tlabel(parent, carrier_key, "中国联通 4G", "China Unicom 4G",
           55, 14, 140, 24, 13, charset=CARRIER_CHARSET)
    label(parent, "MOSAICO", 170, 13, 140, 24, 15, TEXT, "center", True)
    tag = "home" if key is None else key
    image(parent, "bolt", 368, 15, 16, 18, GREEN,
          name=f"bolt_{tag}", bind=f"bolt_{tag}",
          bind_target="visible", hidden=True)
    image(parent, "battery", 386, 14, 22, 20, MUTED)
    label(parent, "82%", 410, 14, 38, 24, 13, TEXT, "right",
          name=f"bat_{tag}", bind=f"bat_{tag}", charset="0123456789%")
    if title:
        tlabel(parent, key, title, en, 24, 42, 240, 34, 24, TEXT, "left", True)


NAV = (
    ("首页", "Home", "nav_home"),
    ("Wi-Fi", "Wi-Fi", "nav_wifi"),
    ("设备", "Devices", "nav_devices"),
    ("蜂窝", "Cellular", "nav_cellular"),
    ("系统", "System", "nav_system"),
)


def nav(parent, selected):
    box(parent, 0, 424, 480, 56, "#09131F", 0)
    box(parent, 0, 424, 480, 1, "#182636", 0)
    nav_icons = ("home", "wifi", "device", "signal", "gear")
    for i, ((zh, en, action), icon_name) in enumerate(zip(NAV, nav_icons)):
        active = i == selected
        bx = 24 + i * 88
        button(
            parent, "", bx, 426, 80, 50, action,
            "#09131F",
            CYAN if active else MUTED, 0
        )
        image(parent, icon_name, bx + 29, 430, 22, 22,
              CYAN if active else MUTED)
        tlabel(parent, f"nav_{selected}_{i}", zh, en, bx + 3, 453, 74, 18,
               12, CYAN if active else MUTED, "center")
        if active:
            box(parent, bx + 26, 473, 28, 3, CYAN, 2)


def back_header(parent, key, zh, en, button_name=None):
    button(parent, "‹", 28, 18, 48, 40, "back", BG, CYAN, 20,
           font_size=30, name=button_name)
    tlabel(parent, key, zh, en, 86, 18, 308, 40, 25, TEXT, "center", True)


def row(parent, y, title, value="", action=None, color=PANEL,
        key=None, en_title=None, en_value="", h=48, chevron=False):
    if key and action:
        tbutton(parent, key, f"{title}    {value}".strip(),
                f"{en_title}    {en_value}".strip(),
                24, y, 432, h, action, color, TEXT)
        return
    box(parent, 24, y, 432, h, color, 12)
    text_y = y + max(0, (h - 24) // 2)
    value_w = 168 if chevron else 192
    if key:
        tlabel(parent, f"{key}_title", title, en_title, 40, text_y, 210, 24, 15)
        tlabel(parent, f"{key}_value", value, en_value, 220, text_y, value_w, 24,
               14, MUTED, "right")
    else:
        label(parent, title, 40, text_y, 210, 24, 15)
        label(parent, value, 220, text_y, value_w, 24, 14, MUTED, "right")
    if chevron:
        image(parent, "chevron", 424, y + (h - 14) // 2, 14, 14, MUTED)


def track(parent, x, y, w, h, ratio, fill=CYAN):
    box(parent, x, y, w, h, "#243246", h // 2)
    box(parent, x, y, max(8, int(w * ratio)), h, fill, h // 2)


def toggle(parent, x, y, on, bind):
    track = box(
        parent, x, y, 46, 26, GREEN if on else "#445164", 13,
        name=f"{bind}_{'on' if on else 'off'}",
        bind=f"{bind}_{'on' if on else 'off'}",
        bind_target="visible", hidden=not on
    )
    box(track, 23 if on else 3, 3, 20, 20, "#FFFFFF", 10)


def build_home():
    p = layer("home", True)
    page_background(p)
    header(p)
    label(p, "●  网络良好", 110, 43, 260, 28, 16, GREEN, "center",
          name="network_status", bind="network_status",
          charset=text_charset(
              "●  网络良好", "●  Connected",
              "●  正在重拨", "●  Redialing",
              "●  请插入 SIM 卡", "●  Insert SIM card",
              "●  请接入 4G 子板", "●  Insert 4G module",
              "●  正在拨号...", "●  Dialing..."))
    label(p, "7.8", 86, 67, 240, 90, 78, CYAN, "right", True,
          name="download_speed", bind="download_speed",
          charset="0123456789.")
    label(p, "Mbps", 330, 102, 110, 42, 30, CYAN, "left", True)
    tlabel(p, "download_caption", "实时下载", "Download",
           170, 153, 140, 25, 16, MUTED, "center")
    label(p, "↑ 1.2 Mbps", 170, 177, 140, 24, 15, CYAN, "center",
          name="upload_speed", bind="upload_speed",
          charset="↑ 0123456789.Mbps")

    add(
        "image", p, name="speed_chart", bind="speed_chart",
        x=24, y=204, w=432, h=32,
        image="assets/speed_chart.png", codec="lossless",
    )

    image(p, "signal", 26, 242, 24, 26, CYAN)
    tlabel(p, "month_caption", "本月", "Data", 54, 244, 42, 24, 14, TEXT)
    label(p, "18.6 GB", 96, 241, 66, 28, 18, TEXT, "right", True,
          name="home_month_value", bind="home_month_value",
          charset="0123456789. KMGBlB-")
    box(p, 174, 241, 1, 30, LINE, 0)
    image(p, "device", 186, 242, 26, 26, CYAN)
    tlabel(p, "home_devices_caption", "设备", "Devices", 216, 244, 56, 24, 14, TEXT)
    label(p, "8 台", 272, 241, 38, 28, 18, TEXT, "right", True,
          name="device_count", bind="device_count",
          charset="0123456789 台")
    box(p, 322, 241, 1, 30, LINE, 0)
    image(p, "battery", 334, 242, 26, 26, CYAN)
    tlabel(p, "runtime_caption", "续航", "Battery", 364, 244, 52, 24, 14, TEXT)
    tlabel(p, "runtime_value", "约 8h", "~8h", 408, 241, 52, 28, 16, TEXT, "right", True,
           charset=text_charset("约 8h", "~8h", "充电中", "Chg", "已接电", "PWR", "0123456789hm~.% "))

    button(
        p, "　分享 Wi-Fi", 24, 278, 208, 48, "share_wifi", BLUE, TEXT,
        name="share_wifi", bind="share_wifi",
        font_charset=text_charset("　分享 Wi-Fi", "　Share Wi-Fi",
                                  "　热点已关闭", "　Hotspot off")
    )
    image(p, "qr", 50, 290, 24, 24, TEXT)
    tbutton(p, "redial_button", "　重新拨号", "　Redial",
            248, 278, 208, 48, "redial")
    image(p, "refresh", 274, 290, 24, 24, CYAN)
    hotspot = box(p, 24, 334, 432, 62, PANEL, 16)
    button(hotspot, "", 0, 0, 432, 62, "toggle_hotspot", PANEL, TEXT, 16)
    image(hotspot, "wifi", 18, 17, 30, 30, CYAN)
    label(hotspot, "Mosaico_4G", 58, 8, 220, 24, 16, TEXT, "left", True,
          name="hotspot_ssid", bind="hotspot_ssid", charset=SSID_ALPHABET,
          overflow="ellipsis")
    label(hotspot, "4G 热点已开启", 58, 32, 220, 22, 13, MUTED,
          name="hotspot_status", bind="hotspot_status",
          charset=text_charset("4G 热点已开启", "4G hotspot is on",
                               "4G 热点已关闭", "4G hotspot is off"))
    button(hotspot, "", 346, 10, 74, 42, "toggle_hotspot", PANEL, TEXT, 14)
    toggle(hotspot, 362, 18, True, "hotspot_toggle")
    toggle(hotspot, 362, 18, False, "hotspot_toggle")
    nav(p, 0)


def security_option(parent, text, y, index, selected):
    event = [{
        "event": "click", "action": "call",
        "target_name": "set_security", "arg": index,
    }]
    add(
        "button", parent, text=text, x=0, y=y, w=206, h=30,
        font_size=14, fg_color=TEXT, bg_color="#2A3644", radius=0, font=FONT,
        font_charset=text, text_align="center", events=event,
    )
    add(
        "button", parent, text=text, x=0, y=y, w=206, h=30,
        name=f"sec_on_{index}", bind=f"sec_on_{index}", bind_target="visible",
        hidden=not selected, font_size=14, fg_color=TEXT, bg_color=BLUE,
        radius=0, font=FONT, font_charset=text, text_align="center", events=event,
    )


def build_security_menu(parent):
    menu = add(
        "container", parent, x=234, y=126, w=206, h=150,
        name="security_menu", bind="security_menu", bind_target="visible",
        hidden=True, bg_color="#2A3644", radius=8,
    )
    for index, mode in enumerate(AUTH_MODES):
        security_option(menu, mode, index * 30, index, index == 4)


CHANNELS_ZH = ("自动",) + tuple(f"信道 {ch}" for ch in range(1, 14))
CHANNELS_EN = ("Auto",) + tuple(f"Ch {ch}" for ch in range(1, 14))


def build_channel_menu(parent):
    menu = add(
        "container", parent, x=214, y=120, w=226, h=206,
        name="channel_menu", bind="channel_menu", bind_target="visible",
        hidden=True, bg_color="#2A3644", border_color="#3C4C60",
        border_width=1, radius=10,
    )
    for index in range(14):
        col = index % 2
        row_idx = index // 2
        bx = 6 + col * 109
        by = 6 + row_idx * 28
        zh = CHANNELS_ZH[index]
        en = CHANNELS_EN[index]
        event = [{
            "event": "click", "action": "call",
            "target_name": "set_channel", "arg": index,
        }]
        tbutton(
            menu, f"ch_off_{index}", zh, en,
            bx, by, 105, 25, "set_channel", "#223244", TEXT, 6,
            font_size=13, events=event,
        )
        on_box = add(
            "container", menu, x=bx, y=by, w=105, h=25,
            name=f"ch_on_{index}", bind=f"ch_on_{index}",
            bind_target="visible", hidden=(index != 6),
            bg_color=BLUE, radius=6,
        )
        tbutton(
            on_box, f"ch_on_lbl_{index}", zh, en,
            0, 0, 105, 25, "set_channel", BLUE, TEXT, 6,
            font_size=13, events=event,
        )


def build_wifi():
    p = layer("wifi")
    page_background(p)
    header(p, "Wi-Fi 设置", "title_wifi", "Wi-Fi")
    label(p, "已开启", 378, 48, 78, 28, 15, GREEN, "right",
          name="wifi_enabled", bind="wifi_enabled",
          charset=text_charset("已开启", "On", "已关闭", "Off"))
    box(p, 24, 84, 432, 126, PANEL, 16)
    label(p, "Mosaico_4G", 40, 98, 280, 30, 21, TEXT, "left", True,
          name="wifi_ssid", bind="wifi_ssid", charset=SSID_ALPHABET,
          overflow="ellipsis")
    label(p, "密码  ••••••••", 40, 132, 200, 24, 14, MUTED,
          name="wifi_password", bind="wifi_password",
          charset=text_charset("密码", "Password", "•", SSID_ALPHABET),
          overflow="ellipsis")
    button(p, "", 236, 126, 32, 32, "toggle_password", PANEL, TEXT, 8,
           name="wifi_eye")
    image(p, "eye", 243, 133, 18, 18, MUTED)
    button(p, "", 326, 90, 112, 118, "open_qr_modal", PANEL, TEXT, 10,
           name="wifi_qr_btn")
    add(
        "image", p, name="wifi_qr_surface", bind="wifi_qr_surface",
        x=336, y=96, w=92, h=92,
        image="assets/qr_surface.png", codec="lossless",
    )
    tlabel(p, "wifi_scan", "点击放大", "Tap to zoom", 330, 190, 104, 20, 12, CYAN, "center")
    settings_card = box(p, 24, 218, 432, 196, PANEL, 16)
    for div_y in (49, 98, 147):
        box(settings_card, 16, div_y, 400, 1, LINE, 0)
    tlabel(p, "wifi_name_title", "热点名称", "Hotspot name",
           40, 230, 150, 24, 15)
    label(p, "Mosaico_4G", 180, 230, 200, 24, 14, MUTED, "right",
          name="wifi_name_value", bind="wifi_name_value", charset=SSID_ALPHABET,
          overflow="ellipsis")
    button(p, "›", 388, 225, 56, 34, "open_hotspot", PANEL, MUTED, 8,
           name="hotspot_name_row", font_size=22)
    tlabel(p, "wifi_security_title", "安全", "Security", 40, 279, 90, 24, 15)
    tlabel(p, "wifi_band_title", "2.4G 信道", "2.4G Channel",
           40, 328, 150, 24, 15)
    tlabel(p, "wifi_max_conn_title", "最大连接数", "Max devices",
           40, 377, 160, 24, 15)
    button(
        p, "8 台  ›", 248, 372, 192, 34, "cycle_max_conn", PANEL, MUTED, 8,
        name="wifi_max_conn_value", bind="wifi_max_conn_value",
        font_size=14, text_align="right",
        font_charset=text_charset(
            "4 台  ›", "4  ›", "8 台  ›", "8  ›", "10 台  ›", "10  ›"),
    )
    nav(p, 1)
    button(
        p, "WPA2/WPA3-Personal   ", 234, 274, 206, 34, "open_security",
        "#223244", TEXT, 8, name="wifi_security_value", bind="wifi_security_value",
        font_size=14, text_align="center", font_charset=text_charset(*AUTH_MODES),
    )
    image(p, "caret", 418, 283, 15, 15, MUTED)
    button(
        p, "信道 6   ", 296, 323, 144, 34, "open_channel",
        "#223244", TEXT, 8, name="wifi_channel_value", bind="wifi_channel_value",
        font_size=14, text_align="center",
        font_charset=text_charset(*CHANNELS_ZH, *CHANNELS_EN, "()"),
    )
    image(p, "caret", 418, 332, 15, 15, MUTED)
    build_security_menu(p)
    build_channel_menu(p)
    build_qr_modal(p)


def build_qr_modal(parent):
    modal = add(
        "container", parent, x=60, y=56, w=360, h=356,
        name="qr_modal", bind="qr_modal", bind_target="visible",
        hidden=True, bg_color="#132234", border_color=CYAN,
        border_width=1, radius=18,
    )
    tlabel(modal, "qr_modal_title", "扫码连接 Wi-Fi", "Scan to Join Wi-Fi",
           24, 14, 270, 28, 18, TEXT, "left", True)
    button(modal, "×", 304, 10, 40, 34, "close_qr_modal", PANEL, MUTED, 10,
           font_size=20)
    add(
        "image", modal, name="wifi_qr_large_surface", bind="wifi_qr_large_surface",
        x=80, y=52, w=200, h=200,
        image="assets/qr_large_surface.png", codec="lossless",
    )
    label(modal, "Mosaico_4G", 24, 260, 312, 26, 17, CYAN, "center", True,
          name="qr_modal_ssid", bind="qr_modal_ssid", charset=SSID_ALPHABET,
          overflow="ellipsis")
    tbutton(modal, "qr_modal_close_btn", "关闭", "Close",
            96, 296, 168, 42, "close_qr_modal", BLUE, TEXT, 12,
            font_size=15)


def build_device_row():
    row_template = add(
        "container", x=0, y=0, w=432, h=72, template="device_row",
        bg_color=PANEL, radius=0
    )
    raster(row_template, "icon_phone", 14, 17, 38, 38,
           name="device_icon", dynamic_image=True)
    label(row_template, "设备", 64, 6, 200, 22, 16, TEXT, "left", True,
          name="device_name", bind="device_name",
          charset=text_charset(
              SSID_ALPHABET, "接入设备", "Client",
              "Mali 的手机", "Mali's Phone", "MacBook Pro",
              "客厅平板", "Living Room Tablet", "书房电脑", "Study PC",
              "客厅手表", "Living Room Watch", "门口摄像头", "Door Camera",
              "访客手机", "Guest Phone", "iPad mini"),
          overflow="ellipsis")
    label(row_template, "192.168.4.2 · 0 MB · 0m", 64, 29, 236, 18, 13, MUTED,
          name="device_ip", bind="device_ip",
          charset="0123456789. KMGBhminsec·",
          overflow="ellipsis")
    label(row_template, "MAC  00:00:00:00:00:00", 64, 48, 236, 16, 11, "#728296",
          name="device_mac", bind="device_mac",
          charset="MAC 0123456789ABCDEF:",
          overflow="ellipsis")
    label(row_template, "● 在线", 282, 24, 104, 22, 13, GREEN, "right",
          name="device_status", bind="device_status",
          charset=text_charset("● 在线", "● Online", "● 已暂停", "● Paused"),
          overflow="ellipsis", dynamic_color=True)
    add(
        "button", row_template, x=388, y=18, w=38, h=36,
        bg_color=PANEL, radius=8,
        events=[{"event": "click", "action": "call", "target_name": "open_device_detail"}],
    )
    image(row_template, "chevron", 400, 29, 14, 14, scene_svg=False)
    box(row_template, 18, 71, 396, 1, LINE, 0)
    return row_template


def build_device_modals(parent):
    detail = add(
        "container", parent, x=48, y=108, w=384, h=188,
        name="device_modal", bind="device_modal", bind_target="visible",
        hidden=True, bg_color="#132234", border_color=CYAN,
        border_width=1, radius=16,
    )
    label(detail, "Mali 的手机", 18, 12, 300, 26, 18, TEXT, "left", True,
          name="detail_name", bind="detail_name",
          charset=text_charset(
              SSID_ALPHABET, "接入设备", "Client",
              "Mali 的手机", "Mali's Phone", "MacBook Pro",
              "客厅平板", "Living Room Tablet", "书房电脑", "Study PC",
              "客厅手表", "Living Room Watch", "门口摄像头", "Door Camera",
              "访客手机", "Guest Phone", "iPad mini"),
          overflow="ellipsis")
    button(detail, "×", 332, 8, 36, 30, "close_device_modal", PANEL, MUTED, 8,
           font_size=18)
    label(detail, "MAC  3C:22:FB:A1:10:21  ·  192.168.4.2",
          18, 40, 348, 18, 11, MUTED, "left", False,
          name="detail_meta", bind="detail_meta",
          charset="MAC 0123456789ABCDEF:·.", overflow="ellipsis")
    label(detail, "已连 1h 24m  ·  已用 620 MB",
          18, 60, 348, 20, 13, CYAN, "left", False,
          name="detail_online", bind="detail_online",
          charset=text_charset(
              "已连", "Online", "已用", "Used", "0123456789. KMGBhminsec·"),
          overflow="ellipsis")
    box(detail, 18, 86, 348, 1, LINE, 0)
    tlabel(detail, "detail_quota_title", "允许上网时长", "Session limit",
           18, 100, 140, 22, 14, TEXT)
    button(detail, "不限时  ›", 166, 96, 200, 30, "cycle_device_quota",
           PANEL, TEXT, 8, name="detail_quota_value", bind="detail_quota_value",
           font_size=13, text_align="right",
           font_charset=text_charset(
               "不限时  ›", "No limit  ›", "限时 30 分钟  ›", "30 min  ›",
               "限时 1 小时  ›", "1 hour  ›", "限时 2 小时  ›", "2 hours  ›"))
    tbutton(detail, "detail_close_btn", "完成", "Done",
            18, 138, 166, 38, "close_device_modal", PANEL, TEXT, 10,
            font_size=14)
    tbutton(detail, "detail_block_btn", "加入黑名单", "Add to blocklist",
            200, 138, 166, 38, "block_selected_device", AMBER, "#07111D", 10,
            font_size=14)

    bl_modal = add(
        "container", parent, x=48, y=88, w=384, h=228,
        name="blacklist_modal", bind="blacklist_modal", bind_target="visible",
        hidden=True, bg_color="#132234", border_color=CYAN,
        border_width=1, radius=16,
    )
    tlabel(bl_modal, "bl_modal_title", "黑名单管理", "Blocklist",
           18, 10, 260, 24, 17, TEXT, "left", True)
    button(bl_modal, "×", 332, 8, 36, 28, "close_blacklist_modal",
           PANEL, MUTED, 8, font_size=18)
    for slot in range(3):
        ry = 42 + slot * 46
        row_box = add(
            "container", bl_modal, x=16, y=ry, w=352, h=42,
            name=f"bl_row_{slot}", bind=f"bl_row_{slot}",
            bind_target="visible", hidden=slot != 0,
            bg_color=PANEL, radius=10,
        )
        label(row_box, "未知设备", 12, 3, 230, 18, 13, TEXT, "left", True,
              name=f"bl_name_{slot}", bind=f"bl_name_{slot}",
              charset=text_charset(
                  SSID_ALPHABET, "接入设备", "Client",
                  "未知设备", "UnknownPhone",
                  "Mali 的手机", "Mali's Phone", "MacBook Pro",
                  "客厅平板", "Living Room Tablet", "书房电脑", "Study PC",
                  "客厅手表", "Living Room Watch", "门口摄像头", "Door Camera",
                  "访客手机", "Guest Phone", "iPad mini"),
              overflow="ellipsis")
        label(row_box, "MAC  A8:9C:ED:41:02:19", 12, 22, 240, 16, 11, MUTED,
              name=f"bl_mac_{slot}", bind=f"bl_mac_{slot}",
              charset="MAC 0123456789ABCDEF:", overflow="ellipsis")
        tbutton(
            row_box, f"bl_btn_{slot}", "移出", "Unblock",
            264, 7, 76, 28, "unblock_slot", "#16324A", CYAN, 8,
            font_size=12,
            events=[{
                "event": "click", "action": "call",
                "target_name": "unblock_slot", "arg": slot,
            }],
        )
    bl_empty = add(
        "container", bl_modal, x=18, y=104, w=348, h=28,
        name="bl_empty_box", bind="bl_empty_box", bind_target="visible",
        hidden=True, bg_color="#132234", radius=0,
    )
    tlabel(bl_empty, "bl_empty_label", "黑名单为空", "No blocked devices",
           0, 2, 348, 24, 14, MUTED, "center", False)
    tbutton(bl_modal, "bl_done_btn", "关闭", "Close",
            112, 184, 160, 34, "close_blacklist_modal", PANEL, TEXT, 8,
            font_size=13)


def action_card(parent, x, y, w, h, icon_name, key, zh, en, tint,
                action="noop", charset=None):
    tbutton(
        parent, key, f"　　　 {zh}", f"　　　 {en}",
        x, y, w, h, action, PANEL, TEXT, 14,
        font_size=14, text_align="left",
        charset=charset if charset else text_charset(f"　　　 {zh}", f"　　　 {en}"),
        overflow="ellipsis",
    )
    image(parent, icon_name, x + 14, y + (h - 24) // 2, 24, 24, tint)


def build_devices():
    build_device_row()
    p = layer("devices")
    page_background(p)
    image(p, "signal", 30, 14, 20, 20, CYAN)
    tlabel(p, "devices_carrier", "中国联通 4G", "China Unicom 4G",
           55, 14, 140, 24, 13, charset=CARRIER_CHARSET)
    label(p, "MOSAICO", 170, 13, 140, 24, 15, TEXT, "center", True)
    image(p, "bolt", 368, 15, 16, 18, GREEN,
          name="bolt_devices", bind="bolt_devices",
          bind_target="visible", hidden=True)
    image(p, "battery", 386, 14, 22, 20, MUTED)
    label(p, "82%", 410, 14, 38, 24, 13, TEXT, "right",
          name="bat_devices", bind="bat_devices", charset="0123456789%")
    tlabel(p, "devices_title", "在线设备", "Online devices",
           24, 44, 160, 32, 22, TEXT, "left", True)
    badge_zh = add(
        "container", p, x=116, y=48, w=22, h=22,
        name="online_badge_zh", bind="online_badge_zh",
        bind_target="visible", bg_color=CYAN, radius=11,
    )
    label(badge_zh, "8", 0, 1, 22, 20, 13, "#07111D", "center", True,
          name="online_count", bind="online_count", charset="0123456789")
    badge_en = add(
        "container", p, x=184, y=48, w=22, h=22,
        name="online_badge_en", bind="online_badge_en",
        bind_target="visible", hidden=True, bg_color=CYAN, radius=11,
    )
    label(badge_en, "8", 0, 1, 22, 20, 13, "#07111D", "center", True,
          name="online_count_en", bind="online_count_en", charset="0123456789")
    box(p, 24, 82, 432, 224, PANEL, 16, border_color=LINE, border_width=1)
    add(
        "list", p, x=24, y=86, w=432, h=216, name="online_devices",
        item_height=72, item_count=8, row_template="device_row",
        scroll_snapshot=True, bg_color=PANEL, fg_color=TEXT,
        callback="open_device_detail",
    )
    empty = box(
        p, 24, 160, 432, 72, PANEL, 0,
        name="devices_empty", bind="devices_empty",
        bind_target="visible", hidden=True
    )
    tlabel(empty, "devices_empty_title", "没有在线设备", "No devices online",
           0, 8, 432, 28, 18, TEXT, "center", True)
    tlabel(empty, "devices_empty_detail", "打开热点后，手机会出现在这里",
           "Turn the hotspot on to see phones here",
           16, 38, 400, 24, 13, MUTED, "center")
    action_card(
        p, 24, 316, 208, 48, "shield", "blacklist_button",
        "黑名单  1", "Blocklist  1", CYAN, action="open_blacklist",
        charset=text_charset("　黑名单", "　Blocklist", "0123456789 "),
    )
    action_card(
        p, 248, 316, 208, 48, "clock", "schedule_button",
        "上网时间管理", "Time schedule", MUTED, action="cycle_schedule",
        charset=text_charset(
            "　上网时间管理", "　Time schedule",
            "　上网时间 · 不限", "　Time · Always",
            "　上网时间 · 夜间禁网", "　Time · 23-07 off",
            "　上网时间 · 限 2h", "　Time · 2h limit",
            "　上网时间 · 限 1h", "　Time · 1h limit",
        ),
    )
    tbutton(
        p, "pause_button", "　　　暂停所有设备", "　　　Pause all",
        24, 374, 176, 36, "toggle_pause_all", PANEL, TEXT, 10,
        font_size=13, text_align="left",
        border_color="#2A3B50", border_width=1,
        charset=text_charset("　暂停所有设备", "　Pause all",
                             "　恢复所有设备", "　Resume all"),
    )
    image(p, "pause", 38, 384, 16, 16, TEXT)
    nav(p, 2)
    build_device_modals(p)


def field_button(parent, y, action, text, name):
    button(parent, text, 24, y, 432, 44, action, PANEL, TEXT, 12,
           name=name, bind=name, font_size=18, text_align="left",
           font_charset=SSID_ALPHABET + "•", overflow="ellipsis")


KEY_BG = "#1A2C40"
KEY_FN = "#16324A"
KEY_SHIFT = 0x100
KEY_SYMBOL = 0x102
KEY_LETTER = 0x103
KEY_DELETE = 0x08
KEY_OK = 0x0D


def key_button(parent, text, x, y, w, h, arg, name, color=KEY_BG):
    return add(
        "button", parent, text=text, x=x, y=y, w=w, h=h, name=name,
        font_size=15, fg_color=TEXT, bg_color=color, radius=6, font=FONT,
        font_charset=text,
        events=[{
            "event": "click", "action": "call",
            "target_name": "hotspot_key", "arg": arg,
        }],
    )


def key_chars(parent, y, chars, x0, width, prefix):
    gap = 4
    count = len(chars)
    key_w = (width - gap * (count - 1)) // count
    for index, char in enumerate(chars):
        token = char if char.isalnum() else str(ord(char))
        key_button(
            parent, char, x0 + index * (key_w + gap), y, key_w, 34,
            ord(char), f"k{prefix}_{token}",
        )


def build_keypad(parent, letters, prefix):
    key_chars(parent, 4, "1234567890", 6, 432, prefix)
    key_chars(parent, 42, letters[:10], 6, 432, prefix)
    key_chars(parent, 80, letters[10:19], 26, 392, prefix)
    key_button(parent, "大写" if letters[0].islower() else "小写",
               6, 118, 58, 34, KEY_SHIFT, f"k{prefix}_shift", KEY_FN)
    key_chars(parent, 118, letters[19:], 68, 308, prefix)
    key_button(parent, "删除", 380, 118, 58, 34, KEY_DELETE, f"k{prefix}_del", KEY_FN)
    key_button(parent, "符号", 14, 156, 74, 34, KEY_SYMBOL, f"k{prefix}_sym", KEY_FN)
    key_button(parent, "空格", 92, 156, 256, 34, ord(" "), f"k{prefix}_sp")
    key_button(parent, "确定", 352, 156, 78, 34, KEY_OK, f"k{prefix}_ok", BLUE)


def build_symbol_pad(parent):
    key_chars(parent, 4, "1234567890", 6, 432, "s")
    key_chars(parent, 42, "!@#$%^&*()", 6, 432, "s")
    key_chars(parent, 80, "-_=+[]{}|~", 6, 432, "s")
    key_chars(parent, 118, ";:'\",.<>/?", 6, 432, "s")
    key_button(parent, "字母", 14, 156, 74, 34, KEY_LETTER, "ks_abc", KEY_FN)
    key_button(parent, "空格", 92, 156, 174, 34, ord(" "), "ks_sp")
    key_button(parent, "删除", 270, 156, 78, 34, KEY_DELETE, "ks_del", KEY_FN)
    key_button(parent, "确定", 352, 156, 78, 34, KEY_OK, "ks_ok", BLUE)


def build_hotspot_setup():
    p = layer("hotspot")
    page_background(p)
    back_header(p, "title_hotspot", "热点设置", "Hotspot", "hotspot_back")
    tlabel(p, "setup_name_label", "热点名称", "Hotspot name",
           24, 72, 220, 22, 13, MUTED)
    field_button(p, 96, "edit_hotspot_name", "Mosaico_4G", "hotspot_name_input")
    tlabel(p, "setup_pass_label", "密码", "Password",
           24, 148, 220, 22, 13, MUTED)
    field_button(p, 172, "edit_hotspot_pass", "mifi2026", "hotspot_pass_input")
    label(p, "正在编辑名称", 24, 222, 432, 22, 13, MUTED, "left",
          name="setup_note", bind="setup_note",
          charset=text_charset(
              "正在编辑名称", "Editing name",
              "正在编辑密码", "Editing password",
              "密码至少 8 位", "Use at least 8 characters",
              "名称不能为空", "Name cannot be empty",
              "名称过长", "Name is too long",
              "已保存", "Saved"))
    lower = add(
        "container", p, x=18, y=248, w=444, h=196, name="keys_lower",
        bind="keys_lower", bind_target="visible", bg_color="#0B1522", radius=12,
    )
    upper = add(
        "container", p, x=18, y=248, w=444, h=196, name="keys_upper",
        bind="keys_upper", bind_target="visible", hidden=True,
        bg_color="#0B1522", radius=12,
    )
    symbols = add(
        "container", p, x=18, y=248, w=444, h=196, name="keys_symbol",
        bind="keys_symbol", bind_target="visible", hidden=True,
        bg_color="#0B1522", radius=12,
    )
    build_keypad(lower, "qwertyuiopasdfghjklzxcvbnm", "l")
    build_keypad(upper, "QWERTYUIOPASDFGHJKLZXCVBNM", "u")
    build_symbol_pad(symbols)


def build_cellular():
    p = layer("cellular")
    page_background(p)
    header(p, "蜂窝网络", "title_cellular", "Cellular")
    tlabel(p, "cell_registered", "● 已注册", "● Registered",
           310, 48, 144, 26, 14, GREEN, "right",
           charset=text_charset(
               "● 已注册", "● Registered",
               "● 未插卡", "● No SIM",
               "● 未接入", "● Unplugged",
               "● 搜索中", "● Searching"))
    box(p, 24, 84, 432, 112, PANEL, 16)
    image(p, "signal", 44, 118, 42, 42, CYAN)
    tlabel(p, "cell_operator", "中国联通 4G", "China Unicom 4G",
           102, 108, 206, 30, 20, TEXT, "left", True,
           charset=text_charset(
               CARRIER_CHARSET,
               "未检测到 SIM 卡", "No SIM card",
               "未检测到 4G 模组", "No 4G module"))
    tlabel(p, "cell_link", "LTE Band 3 · 已联网", "LTE Band 3 · Online",
           102, 144, 206, 22, 13, MUTED,
           charset=text_charset(
               "LTE Band 3 · 已联网", "LTE Band 3 · Online",
               "请插入 4G SIM 卡", "Insert 4G SIM card",
               "请接入 USB 4G 子板", "Connect USB 4G board",
               "RNDIS 正在拨号...", "RNDIS dialing..."))
    tbutton(p, "cell_redial", "　重新拨号", "　Redial",
            312, 120, 124, 40, "redial", BLUE, TEXT, 12,
            font_size=14)
    image(p, "refresh", 324, 131, 18, 18, TEXT)
    cards = (
        ("cell_sig", "信号质量", "Signal", "良好 (-92dBm)", "Good (-92dBm)",
         text_charset("良好", "Good", "一般", "Fair", "无信号", "No signal", "未接入", "None", "0123456789-()dBm ")),
        ("cell_data", "本月流量", "Data used", "18.6 GB", "18.6 GB",
         text_charset("18.6 GB", "0 MB", "0123456789. KMGBlB-")),
        ("cell_ip", "分配 IP", "WAN IP", "10.68.142.19", "10.68.142.19",
         text_charset("10.68.142.19", "未连接", "Disconnected", "0123456789.")),
        ("sim_card", "SIM 卡", "SIM card", "已就绪", "Ready",
         text_charset("已就绪", "Ready", "未插卡", "No SIM", "未检测", "No SIM")),
    )
    for i, (key, zh_t, en_t, zh_v, en_v, cset) in enumerate(cards):
        x = 24 + (i % 2) * 220
        y = 208 + (i // 2) * 58
        box(p, x, y, 212, 50, PANEL, 12)
        tlabel(p, f"{key}_title", zh_t, en_t, x + 14, y + 5, 184, 18, 12, MUTED)
        tlabel(p, f"{key}_value", zh_v, en_v,
               x + 14, y + 23, 184, 22, 15, TEXT, "left", True, charset=cset)
    row(p, 330, "APN", "CMNET", key="apn",
        en_title="APN", en_value="CMNET")
    roam = box(p, 24, 382, 432, 42, PANEL, 12)
    tbutton(roam, "cell_roaming", "　 数据漫游", "　 Data roaming",
            0, 0, 432, 42, "toggle_roaming", PANEL, TEXT, 12,
            font_size=15, text_align="left")
    toggle(roam, 370, 8, True, "roaming")
    toggle(roam, 370, 8, False, "roaming")
    nav(p, 3)


def system_menu_row(parent, y, icon_name, key, zh, en, action, color=TEXT):
    tbutton(parent, key, f"　　　 {zh}", f"　　　 {en}",
            0, y, 432, 34, action, PANEL, color, 8,
            text_align="left", font_size=15,
            charset=text_charset(
                f"　　　 {zh}", f"　　　 {en}",
                "　　　 网络诊断   无 SIM 卡", "　　　 Diagnostics   No SIM",
                "　　　 网络诊断   未接入模组", "　　　 Diagnostics   Unplugged",
                "　　　 网络诊断   正在拨号", "　　　 Diagnostics   Dialing"))
    image(parent, icon_name, 16, y + 7, 20, 20, color)
    image(parent, "chevron", 400, y + 10, 14, 14,
          AMBER if color == AMBER else MUTED)


def build_reset_modal(parent):
    modal = add(
        "container", parent, x=36, y=142, w=408, h=188,
        name="reset_modal", bind="reset_modal", bind_target="visible",
        hidden=True, bg_color="#162638", border_color=AMBER,
        border_width=1, radius=16,
    )
    image(modal, "alert", 22, 20, 24, 24, AMBER)
    tlabel(modal, "reset_title", "恢复出厂设置？", "Factory reset?",
           54, 18, 330, 28, 18, TEXT, "left", True)
    tlabel(modal, "reset_detail",
           "将恢复默认热点名称、密码、安全方式、亮度与息屏设置。",
           "Resets hotspot name, password, security, brightness and sleep.",
           22, 58, 364, 52, 14, MUTED, "left")
    tbutton(modal, "reset_cancel", "取消", "Cancel",
            22, 128, 174, 42, "cancel_factory_reset", PANEL, TEXT, 10)
    tbutton(modal, "reset_confirm", "确认恢复", "Reset",
            212, 128, 174, 42, "confirm_factory_reset", AMBER, "#07111D", 10)


def build_system():
    p = layer("system")
    page_background(p)
    image(p, "signal", 30, 14, 20, 20, CYAN)
    tlabel(p, "system_carrier", "中国联通 4G", "China Unicom 4G",
           55, 14, 140, 24, 13, charset=CARRIER_CHARSET)
    label(p, "MOSAICO", 170, 13, 140, 24, 15, TEXT, "center", True)
    image(p, "bolt", 368, 15, 16, 18, GREEN,
          name="bolt_system", bind="bolt_system",
          bind_target="visible", hidden=True)
    image(p, "battery", 386, 14, 22, 20, MUTED)
    label(p, "82%", 410, 14, 38, 24, 13, TEXT, "right",
          name="bat_system", bind="bat_system", charset="0123456789%")
    tlabel(p, "title_system", "系统", "System", 24, 40, 180, 30, 24, TEXT, "left", True)
    tbutton(p, "language_row", "　 中文", "　 EN",
            348, 42, 108, 28, "toggle_language", PANEL, CYAN, 14,
            font_size=14, text_align="center")
    image(p, "globe", 360, 47, 18, 18, CYAN)

    battery = box(p, 24, 74, 432, 58, PANEL, 16)
    image(battery, "battery", 16, 8, 24, 24, TEXT)
    box(battery, 22, 16, 8, 8, GREEN, 1)
    image(battery, "bolt", 40, 10, 14, 16, GREEN,
          name="bolt_sys_card", bind="bolt_sys_card",
          bind_target="visible", hidden=True)
    tlabel(battery, "battery_summary", "82% · 约 8 小时", "82% · about 8 h",
           56, 8, 270, 24, 16, TEXT, "left", True,
           charset=text_charset(
               "82% · 约 8 小时", "82% · about 8 h",
               "充电中", "Charging", "已充满", "Full",
               "外部供电中", "Ext Power",
               "预计充满", "full in", "分钟", "min",
               "USB 供电", "USB Power",
               "0123456789%.·h() "))
    image(battery, "thermo", 16, 32, 20, 20, MUTED)
    tlabel(battery, "battery_detail", "内部温度 38°C  正常", "Board 38°C  Normal",
           56, 30, 172, 22, 13, TEXT,
           charset=text_charset(
               "内部温度 38°C  正常", "Board 38°C  Normal",
               "板温", "电池温度", "芯片温度", "Bat", "SoC",
               "偏高", "Warm", "0123456789.°C- "))
    tlabel(battery, "battery_hw", "放电中 · 4.02 V", "Discharging · 4.02 V",
           228, 30, 188, 22, 12, MUTED, "right",
           charset=text_charset(
               "放电中", "Discharging", "充电中", "Charging",
               "外部供电", "Ext Power", "电池供电", "Battery",
               "USB 外部供电", "USB Powered", "0123456789.V·mA+-() "))

    power = box(p, 24, 140, 432, 102, PANEL, 16)
    tbutton(power, "power_save", "　　　 省电模式", "　　　 Power saving",
            0, 0, 432, 34, "toggle_power_save", PANEL, TEXT, 12,
            font_size=15, text_align="left")
    image(power, "power", 16, 8, 20, 20, TEXT)
    toggle(power, 370, 4, True, "power_save")
    toggle(power, 370, 4, False, "power_save")
    box(power, 56, 34, 360, 1, LINE, 0)
    image(power, "sun", 16, 42, 20, 20, TEXT)
    tlabel(power, "brightness", "屏幕亮度", "Brightness", 56, 40, 88, 24, 15, TEXT)
    add(
        "slider", power, name="brightness_slider",
        x=148, y=38, w=194, h=28,
        min=10, max=100, value=70,
        track_size=8, radius=14,
        bg_color="#243246", fg_color=CYAN, knob_color="#FFFFFF",
        events=[{
            "event": "value", "action": "call", "target_name": "set_brightness",
        }],
    )
    label(power, "70%", 346, 40, 66, 24, 14, TEXT, "right",
          name="brightness_value", bind="brightness_value", charset="0123456789%")
    box(power, 56, 68, 360, 1, LINE, 0)
    image(power, "clock", 16, 76, 20, 20, TEXT)
    tlabel(power, "sleep_row", "自动息屏", "Auto sleep",
           56, 74, 130, 24, 15, TEXT)
    button(
        power, "常亮  ›", 248, 70, 168, 30, "cycle_sleep", PANEL, MUTED, 8,
        name="sleep_value", bind="sleep_value", font_size=14, text_align="right",
        font_charset=text_charset(
            "15 秒  ›", "15 sec  ›", "30 秒  ›", "30 sec  ›",
            "1 分钟  ›", "1 min  ›", "5 分钟  ›", "5 min  ›",
            "常亮  ›", "Never  ›"
        ),
    )

    menu = box(p, 24, 248, 432, 170, PANEL, 16)
    entries = (
        ("wrench", "diag_row", "网络诊断   全部正常", "Diagnostics   All healthy",
         "open_diagnostics", TEXT),
        ("upload", "update_row", "系统更新   已是最新", "System update   Up to date",
         "open_update", TEXT),
        ("logs", "logs_row", "运行日志", "Runtime logs", "open_logs", TEXT),
        ("info", "about_row", "关于设备", "About", "open_about", TEXT),
        ("refresh", "factory_reset", "恢复出厂设置", "Factory reset",
         "open_reset_confirm", AMBER),
    )
    for index, (icon_name, key, zh, en, action, color) in enumerate(entries):
        system_menu_row(menu, index * 34, icon_name, key, zh, en, action, color)
    for index in range(1, len(entries)):
        box(menu, 56, index * 34, 360, 1, LINE, 0)
    nav(p, 4)
    build_reset_modal(p)


def build_diagnostics():
    p = layer("diagnostics")
    page_background(p)
    back_header(p, "title_diagnostics", "网络诊断", "Diagnostics")
    # Status summary banner card
    status_card = box(p, 24, 68, 432, 62, PANEL, 14)
    box(status_card, 18, 17, 28, 28, GREEN, 14)
    image(status_card, "check", 24, 23, 16, 16, "#FFFFFF")
    tlabel(status_card, "diag_ok", "网络连接正常", "Connection is healthy",
           58, 10, 354, 24, 18, TEXT, "left", True,
           charset=text_charset("网络连接正常", "Connection is healthy",
                                "正在拨号连接...", "Dialing...",
                                "请插入 SIM 卡", "Insert SIM card",
                                "4G 模组未接入", "4G module unplugged"))
    tlabel(status_card, "diag_when", "刚刚检测", "Checked just now",
           58, 34, 354, 20, 12, MUTED, "left",
           charset=text_charset("刚刚检测", "Checked just now",
                                "正在检测...", "Checking...",
                                "检测完成 · 刚刚", "Checked · just now"))

    # 4-stage inspection card (clean horizontal rows with dividers)
    stages = (
        ("diag_modem", "4G 模组", "Modem", "已连接", "Connected"),
        ("diag_cell", "蜂窝网络", "Cellular", "中国联通 4G", "China Unicom 4G"),
        ("diag_rndis", "RNDIS 网卡", "RNDIS", "192.168.43.100", "192.168.43.100"),
        ("diag_net", "互联网连通", "Internet", "DNS 与公网正常", "DNS and WAN OK"),
    )
    list_card = box(p, 24, 140, 432, 184, PANEL, 14)
    for i, (key, zh, en, zh_detail, en_detail) in enumerate(stages):
        ry = i * 46
        if i > 0:
            box(list_card, 48, ry, 366, 1, LINE, 0)
        box(list_card, 16, ry + 13, 20, 20, GREEN, 10)
        image(list_card, "check", 19, ry + 16, 14, 14, "#FFFFFF")
        tlabel(list_card, f"{key}_title", zh, en,
               48, ry + 11, 130, 24, 15, TEXT, "left", True)
        tlabel(list_card, f"{key}_detail", zh_detail, en_detail,
               182, ry + 11, 232, 24, 14, CYAN, "right",
               charset=text_charset(
                   zh_detail, en_detail, CARRIER_CHARSET,
                   "未连接 (请接入 4G 子板)", "Unplugged",
                   "已连接 (ML307C)", "Connected (ML307C)",
                   "无 SIM 卡", "No SIM card",
                   "未插 SIM 卡", "SIM not inserted",
                   "DNS 与公网正常", "DNS and WAN OK",
                   "等待 WAN 地址", "Waiting for WAN",
                   "未就绪", "Not ready", "等待拨号", "Waiting",
                   "0123456789."))

    # 3 metric badges row
    m1 = box(p, 24, 334, 136, 42, PANEL, 10)
    m2 = box(p, 172, 334, 136, 42, PANEL, 10)
    m3 = box(p, 320, 334, 136, 42, PANEL, 10)
    tlabel(m1, "diag_rsrp", "RSRP  -92 dBm", "RSRP  -92 dBm",
           4, 9, 128, 24, 13, MUTED, "center",
           charset=text_charset("RSRP  -- dBm", "RSRP  -92 dBm", "-0123456789 "))
    tlabel(m2, "diag_sinr", "SINR  18 dB", "SINR  18 dB",
           4, 9, 128, 24, 13, MUTED, "center",
           charset=text_charset("SINR  -- dB", "SINR  18 dB", "-0123456789 "))
    tlabel(m3, "diag_drop", "最近掉线  0 次", "Drops  0",
           4, 9, 128, 24, 13, MUTED, "center",
           charset=text_charset("最近掉线  无", "Last drop  none",
                                "最近掉线  0 次", "Drops  0", "0123456789 "))

    tbutton(p, "diag_recheck", "重新检测", "Check again",
            116, 390, 248, 44, "recheck_diagnostics", BLUE, TEXT, 14)


def build_update():
    p = layer("update")
    page_background(p)
    back_header(p, "title_update", "系统更新", "System update")
    image(p, "shield", 196, 70, 88, 88, CYAN)
    image(p, "check", 226, 98, 28, 28, GREEN)
    image(p, "refresh", 246, 118, 26, 26, CYAN)
    tlabel(p, "update_status", "已是最新版本", "Already up to date",
           70, 158, 340, 38, 24, TEXT, "center", True,
           charset=text_charset("已是最新版本", "Already up to date",
                                "正在检查更新...", "Checking..."))
    row(p, 216, "应用版本", "1.0.0", key="update_app",
        en_title="App version", en_value="1.0.0")
    row(p, 268, "Vibe Mode", "0.2.1", key="update_vibe",
        en_title="Vibe Mode", en_value="0.2.1")
    row(p, 320, "上次检查", "刚刚", key="update_checked",
        en_title="Last check", en_value="Just now")
    tbutton(p, "update_check", "检查更新", "Check for updates",
            92, 382, 296, 46, "check_update", BLUE, TEXT, 14)
    tlabel(p, "update_note", "更新期间热点会暂时中断并自动恢复",
           "The hotspot pauses during an update, then returns",
           40, 442, 400, 24, 12, MUTED, "center")


def build_logs():
    p = layer("logs")
    page_background(p)
    back_header(p, "title_logs", "运行日志", "Runtime logs")
    # Clean real-time event log card (no dead filter tabs or fake export button)
    log_card = box(p, 24, 72, 432, 316, PANEL, 16)
    events = (
        ("log_0", "等待系统事件...", "Waiting for events...", CYAN, "antenna"),
        ("log_1", "Wi-Fi 热点服务已启动", "Wi-Fi SoftAP started", CYAN, "wifi"),
        ("log_2", "USB Host 驱动已就绪", "USB Host driver ready", CYAN, "link"),
        ("log_3", "系统初始化完成", "System initialized", GREEN, "check"),
    )
    for i, (key, zh, en, color, icon_name) in enumerate(events):
        ry = i * 78
        if i > 0:
            box(log_card, 56, ry, 356, 1, LINE, 0)
        image(log_card, icon_name, 18, ry + 25, 24, 24, color)
        tlabel(log_card, key, zh, en, 56, ry + 24, 356, 30, 15, TEXT, "left",
               charset=text_charset(
                   zh, en, UI_CHARSET, SSID_ALPHABET))
    tlabel(p, "logs_note", "实时显示本机最新 4 条硬件与网络事件",
           "Live view of the latest 4 hardware & network events",
           42, 408, 396, 38, 12, MUTED, "center")


def build_about():
    p = layer("about")
    page_background(p)
    back_header(p, "title_about", "关于设备", "About")
    label(p, "MOSAICO", 70, 70, 340, 50, 38, CYAN, "center", True)
    tlabel(p, "about_product", "4G 随身热点", "4G Mobile Hotspot",
           90, 121, 300, 26, 16, CYAN, "center")
    fields = (
        ("about_name", "设备名称", "Device name", "Mosaico MiFi", "Mosaico MiFi"),
        ("about_model", "设备型号", "Model", "ESP32-S31", "ESP32-S31"),
        ("about_modem", "4G 模组", "4G modem", "ML307C", "ML307C"),
        ("about_fw", "模组固件", "Modem firmware", "ML307C-STD", "ML307C-STD"),
        ("about_app", "应用版本", "App version", "1.0.0", "1.0.0"),
        ("about_uptime", "运行时长", "Uptime", "0 分钟", "0 min"),
    )
    info_card = box(p, 24, 154, 432, 234, PANEL, 16)
    for i, (key, zh, en, zh_value, en_value) in enumerate(fields):
        y = 7 + i * 38
        if i > 0:
            box(info_card, 18, i * 38, 396, 1, LINE, 0)
        tlabel(info_card, f"{key}_title", zh, en, 20, y, 130, 24, 14, MUTED)
        tlabel(info_card, f"{key}_value", zh_value, en_value,
               160, y, 252, 24, 15, TEXT, "right",
               charset=text_charset(
                   zh_value, en_value, UI_CHARSET, SSID_ALPHABET))
    tbutton(p, "about_licenses", "开源许可", "Licenses",
            48, 404, 180, 42, "open_licenses", PANEL, CYAN, 12,
            border_color="#2A3B50", border_width=1)
    tbutton(p, "about_privacy", "隐私说明", "Privacy",
            252, 404, 180, 42, "open_privacy", PANEL, CYAN, 12,
            border_color="#2A3B50", border_width=1)


def build_licenses():
    p = layer("licenses")
    page_background(p)
    back_header(p, "title_licenses", "开源许可", "Open-source licenses")
    tlabel(p, "licenses_intro", "本产品使用以下开源软件：",
           "This product uses the following open-source software:",
           28, 72, 424, 26, 14, MUTED)
    entries = (
        ("ESP-IDF", "Apache-2.0"),
        ("ESP-GSP 1.5.1", "Apache-2.0"),
        ("LVGL 9.5.0", "MIT"),
        ("Noto Sans SC", "SIL Open Font License 1.1"),
    )
    for i, (name, license_name) in enumerate(entries):
        y = 112 + i * 62
        box(p, 24, y, 432, 52, PANEL, 12)
        label(p, name, 40, y + 8, 220, 22, 15, TEXT, "left", True)
        label(p, license_name, 200, y + 27, 236, 18, 11, MUTED, "right")
    tlabel(p, "licenses_note", "完整许可文本随固件源码和发布包提供。",
           "Full license texts ship with the source and release package.",
           38, 386, 404, 48, 13, MUTED, "center")


def build_privacy():
    p = layer("privacy")
    page_background(p)
    back_header(p, "title_privacy", "隐私说明", "Privacy")
    paragraphs = (
        ("privacy_0", "Wi-Fi 配置、流量统计和设备名称仅保存在本机。",
         "Wi-Fi setup, usage and device names stay on this device."),
        ("privacy_1", "诊断日志不会记录完整 IMSI、ICCID、IMEI 或密码。",
         "Diagnostics never store a full IMSI, ICCID, IMEI or password."),
        ("privacy_2", "导出诊断信息前会自动脱敏，且仅由用户主动触发。",
         "Exports are redacted and start only when you ask."),
        ("privacy_3", "系统更新只检查版本信息，不分析或上传用户网络流量。",
         "Updates check the version only. Traffic is not uploaded."),
    )
    for i, (key, zh, en) in enumerate(paragraphs):
        y = 82 + i * 78
        box(p, 24, y, 432, 66, PANEL, 12)
        tlabel(p, key, zh, en, 40, y + 10, 400, 46, 14, TEXT)
    tlabel(p, "privacy_reset",
           "恢复出厂设置会清除应用配置，但保留设备身份与 Vibe Mode。",
           "Factory reset clears app settings and keeps identity and Vibe Mode.",
           34, 400, 412, 48, 13, AMBER, "center")


write_svg_assets()
write_device_icons()
build_home()
build_wifi()
build_hotspot_setup()
build_devices()
build_cellular()
build_system()
build_diagnostics()
build_update()
build_logs()
build_about()
build_licenses()
build_privacy()

scene = {
    "screen": "mifi",
    "w": 480,
    "h": 480,
    "screen_bg": BG,
    "font": FONT,
    "default_font_size": 16,
    "swipe": False,
    "objects": objects,
}

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps(scene, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
strings = ["static void apply_scene_strings(struct app_mifi *state)", "{",
           "    const bool en = state->english;"]
for key, zh, en in I18N:
    strings.append(
        "    remember_error(state, gsp_mifi_%s_set_text(state->ui, en ? \"%s\" : \"%s\"));"
        % (key, en.replace('"', '\\"'), zh.replace('"', '\\"'))
    )
strings.append("}")
(ROOT / "components" / "app_mifi" / "ui_strings.inc").write_text(
    "\n".join(strings) + "\n", encoding="utf-8"
)
print(f"generated {OUT} ({len(objects)} objects, {len(I18N)} strings)")
