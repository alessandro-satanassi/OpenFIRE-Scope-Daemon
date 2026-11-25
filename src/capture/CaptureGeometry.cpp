/*  OpenFIRE Scope Daemon: video from the PC to the OpenFIRE scopes.
    Copyright (C) 2026  Alessandro Satanassi, Team OpenFIRE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "capture/CaptureGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace CaptureGeometry {

void textureSize(int W, int H, CaptureRotation r, int *tw, int *th)
{
    const bool swap = r == CaptureRotation::R90 || r == CaptureRotation::R270;
    *tw = swap ? H : W;
    *th = swap ? W : H;
}

void desktopSize(int tw, int th, CaptureRotation r, int *W, int *H)
{
    textureSize(tw, th, r, W, H);   // EN: the same swap / IT: lo stesso scambio
}

void desktopToTexture(int dx, int dy, int W, int H, CaptureRotation r, int *tx, int *ty)
{
    // EN: as in the DesktopDuplication sample of Microsoft: with 90 degrees the texture pixel (tx, ty) is
    //     shown at (W - 1 - ty, tx), with 270 degrees at (ty, H - 1 - tx).
    // IT: come nell'esempio DesktopDuplication di Microsoft: con 90 gradi il pixel (tx, ty) della texture si
    //     vede in (W - 1 - ty, tx), con 270 gradi in (ty, H - 1 - tx).
    switch (r) {
    case CaptureRotation::R90:  *tx = dy;          *ty = W - 1 - dx;  break;
    case CaptureRotation::R180: *tx = W - 1 - dx;  *ty = H - 1 - dy;  break;
    case CaptureRotation::R270: *tx = H - 1 - dy;  *ty = dx;          break;
    default:                    *tx = dx;          *ty = dy;          break;   // R0
    }
}

CropPlan planCrop(double aimX, double aimY, int W, int H, CaptureRotation r, int zoom)
{
    CropPlan p;
    p.side = Side / std::clamp(zoom, 1, 4);
    p.ox = int(std::floor(aimX)) - p.side / 2;
    p.oy = int(std::floor(aimY)) - p.side / 2;
    int ax, ay, bx, by;
    desktopToTexture(p.ox, p.oy, W, H, r, &ax, &ay);
    desktopToTexture(p.ox + p.side - 1, p.oy + p.side - 1, W, H, r, &bx, &by);
    p.bx = std::min(ax, bx);
    p.by = std::min(ay, by);
    int tw, th;
    textureSize(W, H, r, &tw, &th);
    p.x0 = std::max(p.bx, 0);
    p.y0 = std::max(p.by, 0);
    p.x1 = std::min(p.bx + p.side, tw);
    p.y1 = std::min(p.by + p.side, th);
    return p;
}

void cropToRgb(const uint8_t *staging, int pitch, const CropPlan &p, int W, int H, CaptureRotation r, uint8_t *crop)
{
    // EN: the mapping is affine: start point and steps along a desktop row and column.
    // IT: la corrispondenza e' affine: punto iniziale e passi lungo una riga e una colonna del desktop.
    int t0x, t0y, tix, tiy, tjx, tjy;
    desktopToTexture(p.ox, p.oy, W, H, r, &t0x, &t0y);
    desktopToTexture(p.ox + 1, p.oy, W, H, r, &tix, &tiy);
    desktopToTexture(p.ox, p.oy + 1, W, H, r, &tjx, &tjy);
    const int dix = tix - t0x, diy = tiy - t0y, djx = tjx - t0x, djy = tjy - t0y;
    const int S = p.side;
    for (int j = 0; j < S; ++j) {
        uint8_t *out = crop + size_t(j) * S * 3;
        const int dy = p.oy + j;
        if (dy < 0 || dy >= H) {
            std::memset(out, 0, size_t(S) * 3);
            continue;
        }
        int sx = t0x + djx * j - p.bx;
        int sy = t0y + djy * j - p.by;
        for (int i = 0; i < S; ++i, sx += dix, sy += diy, out += 3) {
            const int dx = p.ox + i;
            if (dx < 0 || dx >= W) {
                out[0] = out[1] = out[2] = 0;
                continue;
            }
            const uint8_t *px = staging + size_t(sy) * pitch + size_t(sx) * 4;
            out[0] = px[2];
            out[1] = px[1];
            out[2] = px[0];
        }
    }
}

void scaleTo240(const uint8_t *crop, int side, uint8_t *dst)
{
    constexpr int Stride = Side * 3;
    if (side == Side) {
        std::memcpy(dst, crop, size_t(Side) * Stride);
        return;
    }
    // EN: centres of the output pixels on the source, weights in 1/256. / IT: centri dei pixel di uscita sulla sorgente, pesi in 1/256.
    int i0[Side], i1[Side], w[Side];
    for (int u = 0; u < Side; ++u) {
        double s = (u + 0.5) * side / double(Side) - 0.5;
        s = std::clamp(s, 0.0, double(side - 1));
        const int a = int(s);
        i0[u] = a;
        i1[u] = std::min(a + 1, side - 1);
        w[u] = int(std::lround((s - a) * 256.0));
    }
    // EN: first between the two source rows, then along the row. / IT: prima tra le due righe della sorgente, poi lungo la riga.
    const int srcStride = side * 3;
    int row[Stride];
    for (int v = 0; v < Side; ++v) {
        const uint8_t *r0 = crop + size_t(i0[v]) * srcStride;
        const uint8_t *r1 = crop + size_t(i1[v]) * srcStride;
        const int wy = w[v];
        for (int k = 0; k < srcStride; ++k) row[k] = r0[k] * (256 - wy) + r1[k] * wy;
        uint8_t *out = dst + size_t(v) * Stride;
        for (int u = 0; u < Side; ++u) {
            const int *a = row + i0[u] * 3, *b = row + i1[u] * 3;
            const int wx = w[u];
            *out++ = uint8_t((a[0] * (256 - wx) + b[0] * wx + 32768) >> 16);
            *out++ = uint8_t((a[1] * (256 - wx) + b[1] * wx + 32768) >> 16);
            *out++ = uint8_t((a[2] * (256 - wx) + b[2] * wx + 32768) >> 16);
        }
    }
}

double absoluteToUnit(long value)
{
    return double(std::clamp(value, 0L, 65535L)) / 65536.0;
}

} // namespace CaptureGeometry
