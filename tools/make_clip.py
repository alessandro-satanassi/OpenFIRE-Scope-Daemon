#!/usr/bin/env python3
#  OpenFIRE Scope Daemon: video from the PC to the OpenFIRE scopes.
#  Copyright (C) 2026  Alessandro Satanassi, Team OpenFIRE
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.
"""
EN: Makes a test clip (.ofclip) for "OpenFIRE-Scope-Daemon --play-clip": frames already encoded in the
    scope format, the same as jfmt.py of mirino-sim (15 slices of 16 rows, 4:2:0, tables K.1-K.6, IJG
    scale of q, DC reset at every slice, MCU outside the circle omitted, end of slice padded with 1s,
    pixels outside the visible circle filled with the nearest edge colour).
    Without --images: a synthetic scene (colour bars, moving rings, a counter). With --images DIR: a slow
    pan across the pictures of the folder (jpg/png). Needs numpy and Pillow.
IT: Crea una clip di prova (.ofclip) per "OpenFIRE-Scope-Daemon --play-clip": frame gia' codificati nel
    formato del mirino, lo stesso di jfmt.py di mirino-sim (15 fasce da 16 righe, 4:2:0, tabelle K.1-K.6,
    scala IJG di q, DC azzerato a ogni fascia, MCU fuori dal cerchio omessi, fine fascia riempita con 1,
    pixel fuori dal cerchio visibile riempiti con il colore del bordo piu' vicino).
    Senza --images: una scena sintetica (barre di colore, anelli in movimento, un contatore). Con
    --images CARTELLA: una panoramica lenta sulle immagini della cartella (jpg/png). Servono numpy e Pillow.

File .ofclip: b"OFCLIP1\\0", frames (u16 LE), fps (u16 LE), then for every frame 15 x [q u8, len u16 LE, data].
"""
import argparse
import os
import struct
import sys

import numpy as np

W = H = 240
CX = CY = 119.5
R_VIS = 120.0
R_MCU = 120.5
MAX_SLICE = 1400

ZZ = np.array([
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63])

QL = np.array([16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
               14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
               49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99])
QC = np.full(64, 99)
QC[:32] = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
           24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99]

DC_L_BITS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
DC_C_BITS = [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
DC_VALS = list(range(12))
AC_L_BITS = [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d]
AC_L_VALS = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa]
AC_C_BITS = [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77]
AC_C_VALS = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa]


def build_codes(bits, vals):
    codes = {}
    code = 0
    k = 0
    for ln in range(1, 17):
        for _ in range(bits[ln - 1]):
            codes[vals[k]] = (code, ln)
            code += 1
            k += 1
        code <<= 1
    return codes


CODES = {'dcl': build_codes(DC_L_BITS, DC_VALS), 'dcc': build_codes(DC_C_BITS, DC_VALS),
         'acl': build_codes(AC_L_BITS, AC_L_VALS), 'acc': build_codes(AC_C_BITS, AC_C_VALS)}

YY, XX = np.mgrid[0:H, 0:W]
D2 = (XX - CX) ** 2 + (YY - CY) ** 2
VIS = D2 <= R_VIS ** 2
MCU_CODED = np.zeros((15, 15), bool)
for _my in range(15):
    for _mx in range(15):
        MCU_CODED[_my, _mx] = (D2[_my * 16:_my * 16 + 16, _mx * 16:_mx * 16 + 16] <= R_MCU ** 2).any()

_ang = np.arctan2(YY - CY, XX - CX)
EDGE_X = np.clip(np.rint(CX + 118.5 * np.cos(_ang)), 0, W - 1).astype(int)
EDGE_Y = np.clip(np.rint(CY + 118.5 * np.sin(_ang)), 0, H - 1).astype(int)

_k = np.arange(8)
DCTM = np.sqrt(2 / 8) * np.cos((2 * _k[None, :] + 1) * _k[:, None] * np.pi / 16)
DCTM[0] /= np.sqrt(2)


def qtables(q):
    s = 5000 // q if q < 50 else 200 - 2 * q
    f = lambda t: np.clip((t * s + 50) // 100, 1, 255)
    return f(QL), f(QC)


def to_rgb565_expanded(rgb):
    """The game sends RGB565 at most: quantize and expand, as the encoder of the daemon will see it."""
    c = np.clip(np.rint(rgb), 0, 255).astype(np.uint16)
    r, g, b = c[..., 0] >> 3, c[..., 1] >> 2, c[..., 2] >> 3
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.float64)


def fill_outside(rgb):
    out = rgb.copy()
    m = ~VIS
    out[m] = rgb[EDGE_Y[m], EDGE_X[m]]
    return out


def ycc(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    y = 0.299 * r + 0.587 * g + 0.114 * b
    cb = -0.168736 * r - 0.331264 * g + 0.5 * b + 128
    cr = 0.5 * r - 0.418688 * g - 0.081312 * b + 128
    sub = lambda c: c.reshape(120, 2, 120, 2).mean((1, 3))
    return y, sub(cb), sub(cr)


def quant_blocks(ch, qt):
    h, w = ch.shape
    b = (ch - 128.0).reshape(h // 8, 8, w // 8, 8).transpose(0, 2, 1, 3)
    c = np.einsum('ij,abjk,lk->abil', DCTM, b, DCTM)
    return np.rint(c / qt.reshape(8, 8)).astype(np.int32).reshape(*c.shape[:2], 64)[..., ZZ]


def _cat(v):
    return int(abs(v)).bit_length()


def encode_slice(sy, ycc_planes, q):
    y, cb, cr = ycc_planes
    ql, qc = qtables(q)
    rows = slice(sy * 16, sy * 16 + 16)
    Yq = quant_blocks(y[rows], ql)               # (2, 30, 64)
    Bq = quant_blocks(cb[sy * 8:sy * 8 + 8], qc)  # (1, 15, 64)
    Rq = quant_blocks(cr[sy * 8:sy * 8 + 8], qc)
    acc = 0
    nbits = 0
    preds = [0, 0, 0]
    for mx in range(15):
        if not MCU_CODED[sy, mx]:
            continue
        blocks = [(0, Yq[0, mx * 2]), (0, Yq[0, mx * 2 + 1]), (0, Yq[1, mx * 2]), (0, Yq[1, mx * 2 + 1]),
                  (1, Bq[0, mx]), (2, Rq[0, mx])]
        for comp, zz in blocks:
            lum = 'l' if comp == 0 else 'c'
            diff = int(zz[0]) - preds[comp]
            preds[comp] = int(zz[0])
            s = _cat(diff)
            c, ln = CODES['dc' + lum][s]
            acc = (acc << ln) | c
            nbits += ln
            if s:
                acc = (acc << s) | (diff if diff >= 0 else diff + (1 << s) - 1)
                nbits += s
            last = 0
            for k in (np.nonzero(zz[1:])[0] + 1):
                run = k - last - 1
                while run > 15:
                    c, ln = CODES['ac' + lum][0xF0]
                    acc = (acc << ln) | c
                    nbits += ln
                    run -= 16
                v = int(zz[k])
                s = _cat(v)
                c, ln = CODES['ac' + lum][(run << 4) | s]
                acc = (acc << ln) | c
                nbits += ln
                acc = (acc << s) | (v if v >= 0 else v + (1 << s) - 1)
                nbits += s
                last = k
            if last != 63:
                c, ln = CODES['ac' + lum][0x00]
                acc = (acc << ln) | c
                nbits += ln
    pad = (-nbits) % 8
    acc = (acc << pad) | ((1 << pad) - 1)
    nbits += pad
    return acc.to_bytes(nbits // 8, 'big') if nbits else b''


def encode_frame(rgb, q):
    planes = ycc(fill_outside(to_rgb565_expanded(rgb)))
    out = []
    for sy in range(15):
        qs = q
        data = encode_slice(sy, planes, qs)
        while len(data) > MAX_SLICE and qs > 1:   # only this slice gets a lower q / solo questa fascia abbassa q
            qs = max(1, qs - 5)
            data = encode_slice(sy, planes, qs)
        out.append((qs, data))
    return out


# --------------------------------------------------------------------------- scenes / scene
DIGITS = {'0': '1110111', '1': '0010010', '2': '1011101', '3': '1011011', '4': '0111010',
          '5': '1101011', '6': '1101111', '7': '1010010', '8': '1111111', '9': '1111011'}


def draw_digit(img, d, x, y, s, col):
    seg = DIGITS[d]
    w, h, t = 6 * s, 10 * s, s
    boxes = [(x, y, x + w, y + t), (x, y, x + t, y + h // 2), (x + w - t, y, x + w, y + h // 2),
             (x, y + h // 2 - t // 2, x + w, y + h // 2 + t - t // 2), (x, y + h // 2, x + t, y + h),
             (x + w - t, y + h // 2, x + w, y + h), (x, y + h - t, x + w, y + h)]
    for on, (x0, y0, x1, y1) in zip(seg, boxes):
        if on == '1':
            img[y0:y1, x0:x1] = col


def synthetic(i, n):
    t = i / n
    img = np.zeros((H, W, 3), np.float64)
    # EN: hue wheel that turns / IT: ruota dei colori che gira
    ang = (np.degrees(_ang) + 360 * t) % 360
    hsv_h = ang / 60.0
    c = np.clip(1 - np.abs((hsv_h % 2) - 1), 0, 1)
    sector = hsv_h.astype(int) % 6
    rgb = np.zeros((H, W, 3))
    for s, (a, b2, d) in enumerate([(1, 'c', 0), ('c', 1, 0), (0, 1, 'c'), (0, 'c', 1), ('c', 0, 1), (1, 0, 'c')]):
        m = sector == s
        for ch, v in enumerate((a, b2, d)):
            rgb[..., ch][m] = c[m] if v == 'c' else v
    r = np.sqrt(D2) / 120.0
    img = rgb * (0.35 + 0.65 * np.clip(r, 0, 1))[..., None] * 255
    # EN: rings moving outwards / IT: anelli che si allargano
    rings = (np.sin(2 * np.pi * (r * 4 - t * 3)) > 0.85)
    img[rings] = (255, 255, 255)
    # EN: crosshair / IT: reticolo
    img[118:122, 40:200] = (0, 0, 0)
    img[40:200, 118:122] = (0, 0, 0)
    # EN: frame counter / IT: contatore dei frame
    txt = '%02d' % (i % 100)
    img[150:190, 92:148] = (20, 20, 20)
    for k, d in enumerate(txt):
        draw_digit(img, d, 98 + k * 26, 155, 3, (255, 230, 0))
    return img


def from_images(folder, i, n):
    from PIL import Image
    names = sorted(f for f in os.listdir(folder) if f.lower().endswith(('.jpg', '.jpeg', '.png')))
    if not names:
        sys.exit('no jpg/png in %s' % folder)
    per = max(1, n // len(names))
    name = names[min(len(names) - 1, i // per)]
    im = Image.open(os.path.join(folder, name)).convert('RGB')
    side = min(im.size)
    scale = 1.2 * side / 240.0
    j = (i % per) / per
    cx = im.size[0] / 2 + (j - 0.5) * (im.size[0] - 240 * scale) * 0.8
    cy = im.size[1] / 2
    a = (scale, 0, cx - 120 * scale, 0, scale, cy - 120 * scale)
    return np.asarray(im.transform((240, 240), Image.AFFINE, a, resample=Image.BICUBIC)).astype(np.float64)


def main():
    ap = argparse.ArgumentParser(description='Test clip for OpenFIRE-Scope-Daemon --play-clip')
    ap.add_argument('-o', '--output', default='test_clip.ofclip')
    ap.add_argument('--frames', type=int, default=90)
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('-q', '--quality', type=int, default=60)
    ap.add_argument('--images', help='folder with jpg/png pictures (optional)')
    a = ap.parse_args()
    if not 1 <= a.quality <= 100:
        sys.exit('q must be 1..100')
    total = 0
    with open(a.output, 'wb') as f:
        f.write(b'OFCLIP1\0' + struct.pack('<HH', a.frames, a.fps))
        for i in range(a.frames):
            img = from_images(a.images, i, a.frames) if a.images else synthetic(i, a.frames)
            for q, data in encode_frame(img, a.quality):
                f.write(struct.pack('<BH', q, len(data)) + data)
                total += len(data)
    print('%s: %d frames, %d fps, %.1f KB per frame' % (a.output, a.frames, a.fps, total / a.frames / 1000))


if __name__ == '__main__':
    main()
