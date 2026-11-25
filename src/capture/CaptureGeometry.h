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

#ifndef OFD_CAPTUREGEOMETRY_H
#define OFD_CAPTUREGEOMETRY_H

#include <cstdint>

// EN: Platform independent parts of the screen capture: crop square around the aim point, rotation of the
//     monitor, bilinear enlargement to 240x240 and conversion of the absolute coordinates. Plain C++17.
//     "Desktop" = the monitor as it is shown (W x H, after the rotation); "texture" = the image DXGI gives,
//     in the orientation of the panel (H x W with 90 and 270 degrees).
// IT: Parti della cattura dello schermo indipendenti dal sistema: quadrato da ritagliare attorno al punto di
//     mira, rotazione del monitor, ingrandimento bilineare a 240x240 e conversione delle coordinate assolute.
//     C++17 semplice. "Desktop" = il monitor come si vede (W x H, dopo la rotazione); "texture" = l'immagine
//     che da' DXGI, nel verso del pannello (H x W con 90 e 270 gradi).
enum class CaptureRotation { R0, R90, R180, R270 };

namespace CaptureGeometry {

constexpr int Side = 240;

void textureSize(int W, int H, CaptureRotation r, int *tw, int *th);
void desktopSize(int tw, int th, CaptureRotation r, int *W, int *H);
// EN: desktop pixel -> texture pixel / IT: pixel del desktop -> pixel della texture
void desktopToTexture(int dx, int dy, int W, int H, CaptureRotation r, int *tx, int *ty);

// EN: Square of side 240 / zoom centred on the aim point. In the texture it is still a square of the same
//     side, from (bx, by); only [x0, x1) x [y0, y1) is inside the texture and is copied to the staging
//     texture at (x0 - bx, y0 - by). The rest is black.
// IT: Quadrato di lato 240 / zoom centrato sul punto di mira. Nella texture e' ancora un quadrato dello
//     stesso lato, da (bx, by); solo [x0, x1) x [y0, y1) e' dentro la texture e si copia nella texture di
//     appoggio in (x0 - bx, y0 - by). Il resto e' nero.
struct CropPlan {
    int side = 0;
    int ox = 0, oy = 0;       // EN: first desktop pixel of the square / IT: primo pixel del desktop del quadrato
    int bx = 0, by = 0;       // EN: first texture pixel of the square / IT: primo pixel della texture del quadrato
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};
// EN: aimX, aimY: desktop pixels of the monitor (may be outside). zoom 1..4.
// IT: aimX, aimY: pixel del desktop del monitor (anche fuori). zoom 1..4.
CropPlan planCrop(double aimX, double aimY, int W, int H, CaptureRotation r, int zoom);

// EN: staging: BGRA, the square of the texture from (bx, by), rows of pitch bytes. crop: RGB888 side x side,
//     in the desktop orientation; pixels outside the monitor are black.
// IT: staging: BGRA, il quadrato della texture da (bx, by), righe da pitch byte. crop: RGB888 lato x lato,
//     nel verso del desktop; i pixel fuori dal monitor sono neri.
void cropToRgb(const uint8_t *staging, int pitch, const CropPlan &plan, int W, int H, CaptureRotation r, uint8_t *crop);

// EN: crop (RGB888 side x side) -> dst (RGB888 240 x 240, rows of 720 bytes); side 240 = copy.
// IT: crop (RGB888 lato x lato) -> dst (RGB888 240 x 240, righe da 720 byte); lato 240 = copia.
void scaleTo240(const uint8_t *crop, int side, uint8_t *dst);

// EN: absolute coordinate 0-65535 of Raw Input -> 0-1. / IT: coordinata assoluta 0-65535 di Raw Input -> 0-1.
double absoluteToUnit(long value);

} // namespace CaptureGeometry

#endif // OFD_CAPTUREGEOMETRY_H
