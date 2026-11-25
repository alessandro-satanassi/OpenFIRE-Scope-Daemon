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

#include "capture/ScreenCapture.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>

#include <QCoreApplication>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

#include "app/Log.h"
#include "capture/AimInput.h"
#include "capture/CaptureGeometry.h"
#include "udp/SlotInput.h"

namespace {
constexpr UINT kAcquireTimeoutMs = 8;
constexpr qint64 kRetryMs = 500;
constexpr qint64 kReleaseAfterMs = 2000;   // EN: not needed for so long -> released / IT: non serve da tanto -> si rilascia
constexpr qint64 kCropEveryMs = 16;
constexpr unsigned long kIdleMs = 20;
constexpr int Side = CaptureGeometry::Side;

QString hex(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

// EN: minimal COM reference / IT: riferimento COM minimo
template <class T>
class Com
{
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    T *get() const { return m_p; }
    T *operator->() const { return m_p; }
    T **put() { reset(); return &m_p; }
    void **putVoid() { return reinterpret_cast<void **>(put()); }
    explicit operator bool() const { return m_p != nullptr; }
    void reset()
    {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }

private:
    T *m_p = nullptr;
};

// EN: everything of one duplication, only in the capture thread / IT: tutto di una duplicazione, solo nel thread della cattura
struct Duplication {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGIOutputDuplication> dup;
    Com<ID3D11Texture2D> desktop;   // EN: latest desktop, on the GPU / IT: ultimo desktop, sulla GPU
    Com<ID3D11Texture2D> staging[Settings::Players];
    QString name;
    int left = 0, top = 0;          // EN: position of the monitor in the virtual desktop / IT: posizione del monitor nel desktop virtuale
    int W = 0, H = 0;               // EN: desktop size of the monitor / IT: dimensione del desktop del monitor
    CaptureRotation rotation = CaptureRotation::R0;
    bool haveDesktop = false;

    void reset()
    {
        for (auto &s : staging) s.reset();
        desktop.reset();
        dup.reset();
        context.reset();
        device.reset();
        haveDesktop = false;
    }
};

CaptureRotation rotationOf(DXGI_MODE_ROTATION r)
{
    switch (r) {
    case DXGI_MODE_ROTATION_ROTATE90:  return CaptureRotation::R90;
    case DXGI_MODE_ROTATION_ROTATE180: return CaptureRotation::R180;
    case DXGI_MODE_ROTATION_ROTATE270: return CaptureRotation::R270;
    default:                           return CaptureRotation::R0;
    }
}

// EN: monitor of the game: the one named in the settings, otherwise the primary (origin 0, 0).
// IT: monitor del gioco: quello indicato nelle impostazioni, altrimenti il principale (origine 0, 0).
bool openDuplication(const QString &wanted, Duplication *d, QString *error, bool *fallback)
{
    *fallback = false;
    Com<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.putVoid());
    if (FAILED(hr)) {
        *error = QCoreApplication::translate("ScreenCapture", "DXGI not available (%1)").arg(hex(hr));
        return false;
    }
    int wantedAdapter = -1, wantedOutput = -1, primaryAdapter = -1, primaryOutput = -1;
    Com<IDXGIAdapter1> adapter;
    for (UINT a = 0; SUCCEEDED(factory->EnumAdapters1(a, adapter.put())); ++a) {
        Com<IDXGIOutput> output;
        for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, output.put())); ++o) {
            DXGI_OUTPUT_DESC desc;
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
            const QString name = QString::fromWCharArray(desc.DeviceName);
            if (!wanted.isEmpty() && wantedAdapter < 0 && name.compare(wanted, Qt::CaseInsensitive) == 0) {
                wantedAdapter = int(a);
                wantedOutput = int(o);
            }
            if (primaryAdapter < 0 && desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0) {
                primaryAdapter = int(a);
                primaryOutput = int(o);
            }
        }
    }
    if (wantedAdapter < 0) {
        *fallback = !wanted.isEmpty();
        wantedAdapter = primaryAdapter;
        wantedOutput = primaryOutput;
    }
    if (wantedAdapter < 0) {
        *error = QCoreApplication::translate("ScreenCapture", "no monitor found");
        return false;
    }

    Com<IDXGIOutput> output;
    if (FAILED(factory->EnumAdapters1(UINT(wantedAdapter), adapter.put()))
        || FAILED(adapter->EnumOutputs(UINT(wantedOutput), output.put()))) {
        *error = QCoreApplication::translate("ScreenCapture", "monitor no longer available");
        return false;
    }
    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);

    // EN: the device on the graphics card of the monitor: this also works on PCs with two GPUs.
    // IT: il dispositivo sulla scheda video del monitor: cosi' funziona anche sui PC con due GPU.
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, UINT(std::size(levels)),
                           D3D11_SDK_VERSION, d->device.put(), nullptr, d->context.put());
    if (FAILED(hr)) {
        *error = QCoreApplication::translate("ScreenCapture", "Direct3D 11 device not created (%1)").arg(hex(hr));
        d->reset();
        return false;
    }

    Com<IDXGIOutput5> output5;
    if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput5), output5.putVoid()))) {
        const DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
        hr = output5->DuplicateOutput1(d->device.get(), 0, 1, &format, d->dup.put());
    }
    if (!d->dup) {
        Com<IDXGIOutput1> output1;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), output1.putVoid());
        if (SUCCEEDED(hr)) hr = output1->DuplicateOutput(d->device.get(), d->dup.put());
    }
    if (!d->dup) {
        *error = QCoreApplication::translate("ScreenCapture", "duplication of %1 not available (%2)").arg(QString::fromWCharArray(desc.DeviceName), hex(hr));
        d->reset();
        return false;
    }

    DXGI_OUTDUPL_DESC dupDesc;
    d->dup->GetDesc(&dupDesc);
    d->rotation = rotationOf(dupDesc.Rotation);
    d->name = QString::fromWCharArray(desc.DeviceName);
    d->left = desc.DesktopCoordinates.left;
    d->top = desc.DesktopCoordinates.top;
    d->W = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    d->H = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    d->haveDesktop = false;
    return true;
}

// EN: copies the acquired desktop on the GPU; the textures are created at the first frame and when the
//     size changes. / IT: copia il desktop acquisito sulla GPU; le texture si creano al primo frame e quando
//     cambia la dimensione.
bool copyDesktop(Duplication *d, IDXGIResource *resource, QString *error)
{
    Com<ID3D11Texture2D> texture;
    HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.putVoid());
    if (FAILED(hr)) {
        *error = hex(hr);
        return false;
    }
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        *error = QCoreApplication::translate("ScreenCapture", "desktop format %1 not supported").arg(int(desc.Format));
        return false;
    }
    if (d->desktop) {
        D3D11_TEXTURE2D_DESC current;
        d->desktop->GetDesc(&current);
        if (current.Width != desc.Width || current.Height != desc.Height || current.Format != desc.Format) d->desktop.reset();
    }
    if (!d->desktop) {
        D3D11_TEXTURE2D_DESC dd = {};
        dd.Width = desc.Width;
        dd.Height = desc.Height;
        dd.MipLevels = 1;
        dd.ArraySize = 1;
        dd.Format = desc.Format;
        dd.SampleDesc.Count = 1;
        dd.Usage = D3D11_USAGE_DEFAULT;
        hr = d->device->CreateTexture2D(&dd, nullptr, d->desktop.put());
        if (FAILED(hr)) {
            *error = hex(hr);
            return false;
        }
        D3D11_TEXTURE2D_DESC sd = dd;
        sd.Width = sd.Height = Side;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto &s : d->staging) {
            hr = d->device->CreateTexture2D(&sd, nullptr, s.put());
            if (FAILED(hr)) {
                d->desktop.reset();
                *error = hex(hr);
                return false;
            }
        }
        // EN: the texture is in the orientation of the panel. / IT: la texture e' nel verso del pannello.
        CaptureGeometry::desktopSize(int(desc.Width), int(desc.Height), d->rotation, &d->W, &d->H);
    }
    d->context->CopyResource(d->desktop.get(), texture.get());
    d->haveDesktop = true;
    return true;
}

QHash<QString, QString> friendlyNames()
{
    // EN: GDI name of the source (\\.\DISPLAY1) -> name of the monitor. / IT: nome GDI della sorgente (\\.\DISPLAY1) -> nome del monitor.
    QHash<QString, QString> out;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return out;
    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
        const QString name = QString::fromWCharArray(target.monitorFriendlyDeviceName);
        if (!name.isEmpty()) out.insert(QString::fromWCharArray(source.viewGdiDeviceName).toUpper(), name);
    }
    return out;
}

struct MonitorList {
    QVector<MonitorInfo> *out;
    QHash<QString, QString> names;
};

BOOL CALLBACK addMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto *list = reinterpret_cast<MonitorList *>(data);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    MonitorInfo m;
    m.device = QString::fromWCharArray(info.szDevice);
    m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    const QString name = list->names.value(m.device.toUpper());
    const QString size = QStringLiteral("%1×%2").arg(info.rcMonitor.right - info.rcMonitor.left).arg(info.rcMonitor.bottom - info.rcMonitor.top);
    m.label = name.isEmpty() ? QStringLiteral("%1 (%2)").arg(m.device, size)
                             : QStringLiteral("%1 (%2, %3)").arg(name, m.device, size);
    list->out->append(m);
    return TRUE;
}
} // namespace

ScreenCapture::ScreenCapture(SlotInput *const inputs[Settings::Players], AimInput *aim, QObject *parent)
    : QThread(parent)
    , m_aim(aim)
{
    for (int i = 0; i < Settings::Players; ++i) m_slots[i] = inputs[i];
}

ScreenCapture::~ScreenCapture()
{
    stopCapture();
}

bool ScreenCapture::supported()
{
    return true;
}

QVector<MonitorInfo> ScreenCapture::monitors()
{
    QVector<MonitorInfo> out;
    MonitorList list{ &out, friendlyNames() };
    EnumDisplayMonitors(nullptr, nullptr, addMonitor, reinterpret_cast<LPARAM>(&list));
    std::sort(out.begin(), out.end(), [](const MonitorInfo &a, const MonitorInfo &b) { return a.device < b.device; });
    return out;
}

void ScreenCapture::configure(bool enabled, const QString &monitor, int zoom)
{
    QMutexLocker lock(&m_mutex);
    if (monitor.compare(m_monitor, Qt::CaseInsensitive) != 0) ++m_configSerial;
    m_enabled = enabled;
    m_monitor = monitor;
    m_zoom = std::clamp(zoom, 1, 4);
}

void ScreenCapture::stopCapture()
{
    m_stop.store(true);
    wait();
}

bool ScreenCapture::aimMissing(int player) const
{
    if (player < 1 || player > Settings::Players) return false;
    return m_aimMissing[player - 1].load();
}

void ScreenCapture::run()
{
    Duplication d;
    QString lastError;
    qint64 retryAt = 0;
    qint64 lastNeeded = 0;
    quint64 openedSerial = 0;
    bool newDesktop[Settings::Players] = {};
    bool wasWanted[Settings::Players] = {};
    int lastX[Settings::Players] = {}, lastY[Settings::Players] = {}, lastZoom[Settings::Players] = {};
    qint64 lastCrop[Settings::Players] = {};
    std::vector<uint8_t> crop(size_t(Side) * Side * 3);

    auto setLive = [this](bool on) {
        for (SlotInput *s : m_slots)
            if (s) s->setCaptureLive(on);
    };
    auto release = [&]() {
        setLive(false);
        d.reset();
        for (auto &m : m_aimMissing) m.store(false);
    };
    auto lost = [&](const QString &why, qint64 now) {
        Log::warn(tr("Screen capture lost (%1), trying again every 500 ms").arg(why));
        release();
        lastError.clear();
        retryAt = now + kRetryMs;
    };
    setLive(false);

    while (!m_stop.load()) {
        bool enabled;
        QString monitor;
        int zoom;
        quint64 serial;
        {
            QMutexLocker lock(&m_mutex);
            enabled = m_enabled;
            monitor = m_monitor;
            zoom = m_zoom;
            serial = m_configSerial;
        }
        const qint64 now = monotonicMs();
        bool wanted[Settings::Players] = {};
        int wantedCount = 0;
        for (int p = 0; p < Settings::Players; ++p) {
            wanted[p] = m_slots[p] && m_slots[p]->captureWanted(now);
            if (wanted[p]) ++wantedCount;
            // EN: a slot that enters the capture gets a crop at once, even with a still screen.
            // IT: uno slot che entra in cattura riceve subito un ritaglio, anche a schermo fermo.
            if (wanted[p] && !wasWanted[p]) newDesktop[p] = true;
            wasWanted[p] = wanted[p];
        }
        const bool needed = enabled && wantedCount > 0;
        if (needed) lastNeeded = now;
        if (d.dup && (!enabled || serial != openedSerial || now - lastNeeded >= kReleaseAfterMs)) release();
        if (!needed) {
            for (auto &m : m_aimMissing) m.store(false);
            msleep(kIdleMs);
            continue;
        }

        if (!d.dup) {
            if (now < retryAt) {
                msleep(kIdleMs);
                continue;
            }
            QString error;
            bool fallback = false;
            if (!openDuplication(monitor, &d, &error, &fallback)) {
                if (error != lastError) Log::warn(tr("Screen capture not available: %1. Trying again every 500 ms").arg(error));
                lastError = error;
                retryAt = now + kRetryMs;
                msleep(kIdleMs);
                continue;
            }
            if (fallback) Log::warn(tr("Game monitor %1 not connected: the primary monitor is captured").arg(monitor));
            Log::info(tr("Screen capture of %1 (%2×%3)").arg(d.name).arg(d.W).arg(d.H));
            lastError.clear();
            openedSerial = serial;
            for (int p = 0; p < Settings::Players; ++p) {
                newDesktop[p] = false;
                lastCrop[p] = 0;
            }
            setLive(true);
        }

        DXGI_OUTDUPL_FRAME_INFO info;
        Com<IDXGIResource> resource;
        const HRESULT hr = d.dup->AcquireNextFrame(kAcquireTimeoutMs, &info, resource.put());
        if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
            if (FAILED(hr)) {
                lost(hr == DXGI_ERROR_ACCESS_LOST ? QStringLiteral("DXGI_ERROR_ACCESS_LOST") : hex(hr), now);
                continue;
            }
            // EN: LastPresentTime = 0: only the pointer changed. / IT: LastPresentTime = 0: e' cambiato solo il puntatore.
            QString error;
            bool ok = true, copied = false;
            if (info.LastPresentTime.QuadPart != 0 || !d.haveDesktop) {
                ok = copyDesktop(&d, resource.get(), &error);
                copied = ok;
            }
            resource.reset();
            d.dup->ReleaseFrame();
            if (!ok) {
                lost(error, now);
                continue;
            }
            if (copied)
                for (bool &n : newDesktop) n = true;
        }
        if (!d.haveDesktop) continue;

        // EN: aim points and crops on the GPU, then the reads: the copies of all the slots go together.
        // IT: punti di mira e ritagli sulla GPU, poi le letture: le copie di tutti gli slot vanno insieme.
        const qint64 t = monotonicMs();
        bool due[Settings::Players] = {};
        CaptureGeometry::CropPlan plans[Settings::Players];
        for (int p = 0; p < Settings::Players; ++p) {
            if (!wanted[p]) {
                m_aimMissing[p].store(false);
                continue;
            }
            double x = d.W / 2.0, y = d.H / 2.0;
            bool missing = false;
            const AimInput::Point aim = m_aim ? m_aim->aim(p + 1) : AimInput::Point();
            if (aim.valid) {
                x = aim.x * d.W;   // EN: 0-1 on the game monitor / IT: 0-1 sul monitor del gioco
                y = aim.y * d.H;
            } else if (wantedCount == 1) {
                POINT pt;
                if (GetCursorPos(&pt)) {
                    x = double(pt.x - d.left);
                    y = double(pt.y - d.top);
                }
            } else {
                missing = true;   // EN: centre of the monitor / IT: centro del monitor
            }
            m_aimMissing[p].store(missing);
            const int ix = int(std::floor(x)), iy = int(std::floor(y));
            if (!(newDesktop[p] || ix != lastX[p] || iy != lastY[p] || zoom != lastZoom[p])) continue;
            if (t - lastCrop[p] < kCropEveryMs) continue;
            plans[p] = CaptureGeometry::planCrop(x, y, d.W, d.H, d.rotation, zoom);
            const CaptureGeometry::CropPlan &c = plans[p];
            if (!c.empty()) {
                D3D11_BOX box = { UINT(c.x0), UINT(c.y0), 0, UINT(c.x1), UINT(c.y1), 1 };
                d.context->CopySubresourceRegion(d.staging[p].get(), 0, UINT(c.x0 - c.bx), UINT(c.y0 - c.by), 0,
                                                 d.desktop.get(), 0, &box);
            }
            due[p] = true;
            newDesktop[p] = false;
            lastX[p] = ix;
            lastY[p] = iy;
            lastZoom[p] = zoom;
            lastCrop[p] = t;
        }
        bool failed = false;
        for (int p = 0; p < Settings::Players && !failed; ++p) {
            if (!due[p]) continue;
            const CaptureGeometry::CropPlan &c = plans[p];
            if (c.empty()) {
                std::memset(crop.data(), 0, crop.size());
            } else {
                D3D11_MAPPED_SUBRESOURCE mapped;
                const HRESULT mh = d.context->Map(d.staging[p].get(), 0, D3D11_MAP_READ, 0, &mapped);
                if (FAILED(mh)) {
                    lost(hex(mh), t);
                    failed = true;
                    break;
                }
                CaptureGeometry::cropToRgb(static_cast<const uint8_t *>(mapped.pData), int(mapped.RowPitch), c,
                                           d.W, d.H, d.rotation, crop.data());
                d.context->Unmap(d.staging[p].get(), 0);
            }
            // EN: no free frame: this crop is lost, the next one comes within 16 ms. / IT: nessun frame libero: questo ritaglio si perde, il prossimo arriva entro 16 ms.
            const std::shared_ptr<InputFrame> frame = m_slots[p]->acquire();
            if (!frame) continue;
            CaptureGeometry::scaleTo240(crop.data(), c.side, frame->rgb);
            m_slots[p]->publishCapture(frame);
        }
    }
    release();
}
