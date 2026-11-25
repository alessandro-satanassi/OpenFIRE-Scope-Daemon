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

#include "app/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include "OpenFIRE_Scope_Protocol.h"

namespace {
const char *kDiscoveryPort   = "network/discoveryPort";
const char *kVideoPort       = "network/videoPortP%1";
const char *kCaptureEnabled  = "capture/enabled";
const char *kCaptureMonitor  = "capture/monitor";
const char *kCaptureZoom     = "capture/zoom";
const char *kLightgunMap     = "capture/lightgunMap";
const char *kDongleVid       = "dongle/vid";
const char *kDonglePid       = "dongle/pid";
const char *kSerialPriority  = "threads/serialPriority";
const char *kUdpPriority     = "threads/udpPriority";
const char *kEncoderPriority = "threads/encoderPriority";
const char *kLogToFile       = "log/toFile";
const char *kLogFile         = "log/file";
const char *kWindowGeometry  = "window/geometry";

quint16 validPort(int value, quint16 fallback)
{
    return (value >= 1024 && value <= 65535) ? quint16(value) : fallback;
}

ThreadPriority priorityFrom(const QString &text, ThreadPriority fallback)
{
    if (text.compare(QLatin1String("aboveNormal"), Qt::CaseInsensitive) == 0) return ThreadPriority::AboveNormal;
    if (text.compare(QLatin1String("normal"), Qt::CaseInsensitive) == 0) return ThreadPriority::Normal;
    if (text.compare(QLatin1String("belowNormal"), Qt::CaseInsensitive) == 0) return ThreadPriority::BelowNormal;
    return fallback;
}

// EN: keys of the [encoder] section with their default value. / IT: chiavi della sezione [encoder] con il loro valore predefinito.
struct EncoderKey {
    const char *key;
    double EncoderConfig::*value;
};
const EncoderKey kEncoderDoubles[] = {
    { "encoder/motionLow", &EncoderConfig::motionLow },
    { "encoder/motionHigh", &EncoderConfig::motionHigh },
    { "encoder/instrPerMcu", &EncoderConfig::instrPerMcu },
    { "encoder/instrPerCoef", &EncoderConfig::instrPerCoef },
    { "encoder/instrFixed", &EncoderConfig::instrFixed },
    { "encoder/cpi", &EncoderConfig::cpi },
    { "encoder/dmaWaitMs", &EncoderConfig::dmaWaitMs },
    { "encoder/maxCycleMs", &EncoderConfig::maxCycleMs },
};
struct EncoderIntKey {
    const char *key;
    int EncoderConfig::*value;
};
const EncoderIntKey kEncoderInts[] = {
    { "encoder/profile30Bytes", &EncoderConfig::profile30 },
    { "encoder/profile60Bytes", &EncoderConfig::profile60 },
    { "encoder/qLow", &EncoderConfig::qLow },
    { "encoder/qHigh", &EncoderConfig::qHigh },
};

QVector<LightgunMapEntry> defaultLightgunMap()
{
    QVector<LightgunMapEntry> map;
    for (int p = 1; p <= Settings::Players; ++p)
        map.append({ quint16(OF_LIGHTGUN_VID), quint16(p), p });
    return map;
}
} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_ini(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("OpenFIRE"), QStringLiteral("OpenFIRE-Scope-Daemon"))
{
    // EN: write the priority keys once, so they are visible in the INI and easy to change.
    // IT: scrive una volta le chiavi delle priorita', cosi' sono visibili nell'INI e facili da cambiare.
    if (!m_ini.contains(kSerialPriority)) m_ini.setValue(kSerialPriority, QStringLiteral("aboveNormal"));
    if (!m_ini.contains(kUdpPriority)) m_ini.setValue(kUdpPriority, QStringLiteral("normal"));
    if (!m_ini.contains(kEncoderPriority)) m_ini.setValue(kEncoderPriority, QStringLiteral("normal"));
    // EN: the same for the constants of the encoder. / IT: lo stesso per le costanti dell'encoder.
    const EncoderConfig defaults;
    for (const EncoderKey &k : kEncoderDoubles)
        if (!m_ini.contains(QLatin1String(k.key))) m_ini.setValue(QLatin1String(k.key), defaults.*k.value);
    for (const EncoderIntKey &k : kEncoderInts)
        if (!m_ini.contains(QLatin1String(k.key))) m_ini.setValue(QLatin1String(k.key), defaults.*k.value);
}

QString Settings::filePath() const
{
    return m_ini.fileName();
}

// ----------------------------------------------------------------- network / rete
quint16 Settings::discoveryPort() const
{
    if (m_discoveryOverride) return m_discoveryOverride;
    return validPort(m_ini.value(kDiscoveryPort, OF_UDP_DISCOVERY_PORT).toInt(), OF_UDP_DISCOVERY_PORT);
}

void Settings::setDiscoveryPort(quint16 port)
{
    m_discoveryOverride = 0;   // EN: a value chosen in the window wins / IT: vince il valore scelto nella finestra
    m_ini.setValue(kDiscoveryPort, port);
}

quint16 Settings::preferredVideoPort(int player) const
{
    const quint16 def = quint16(OF_UDP_VIDEO_PORT_P1 + player - 1);
    return validPort(m_ini.value(QString::fromLatin1(kVideoPort).arg(player), def).toInt(), def);
}

void Settings::setPreferredVideoPort(int player, quint16 port)
{
    m_ini.setValue(QString::fromLatin1(kVideoPort).arg(player), port);
}

// ----------------------------------------------------------------- capture / cattura
bool Settings::captureSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool Settings::captureEnabled() const
{
    if (!captureSupported() || m_captureOffOverride) return false;
    return m_ini.value(kCaptureEnabled, true).toBool();
}

void Settings::setCaptureEnabled(bool on)
{
    m_captureOffOverride = false;
    m_ini.setValue(kCaptureEnabled, on);
}

QString Settings::captureMonitor() const
{
    return m_ini.value(kCaptureMonitor, QString()).toString();
}

void Settings::setCaptureMonitor(const QString &name)
{
    m_ini.setValue(kCaptureMonitor, name);
}

int Settings::captureZoom() const
{
    const int zoom = m_ini.value(kCaptureZoom, 2).toInt();
    return (zoom >= 1 && zoom <= 4) ? zoom : 2;
}

void Settings::setCaptureZoom(int zoom)
{
    if (zoom >= 1 && zoom <= 4) m_ini.setValue(kCaptureZoom, zoom);
}

QVector<LightgunMapEntry> Settings::lightgunMap() const
{
    // EN: key missing or not valid = default list; key empty = no lightgun.
    // IT: chiave assente o non valida = lista predefinita; chiave vuota = nessuna lightgun.
    QVector<LightgunMapEntry> map;
    if (m_ini.contains(kLightgunMap) && lightgunMapFromText(m_ini.value(kLightgunMap).toString(), &map)) return map;
    return defaultLightgunMap();
}

void Settings::setLightgunMap(const QVector<LightgunMapEntry> &map)
{
    m_ini.setValue(kLightgunMap, lightgunMapToText(map));
}

// EN: "F143:0001=1;F143:0002=2" / IT: "F143:0001=1;F143:0002=2"
QString Settings::lightgunMapToText(const QVector<LightgunMapEntry> &map)
{
    QStringList parts;
    for (const LightgunMapEntry &e : map)
        parts << QStringLiteral("%1:%2=%3")
                     .arg(e.vid, 4, 16, QLatin1Char('0'))
                     .arg(e.pid, 4, 16, QLatin1Char('0'))
                     .arg(e.player)
                     .toUpper();
    return parts.join(QLatin1Char(';'));
}

bool Settings::lightgunMapFromText(const QString &text, QVector<LightgunMapEntry> *map)
{
    QVector<LightgunMapEntry> out;
    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        const QString part = raw.trimmed();
        const int colon = part.indexOf(QLatin1Char(':'));
        const int equal = part.indexOf(QLatin1Char('='));
        if (colon <= 0 || equal <= colon) return false;
        bool okV = false, okP = false, okN = false;
        const uint vid = part.left(colon).toUInt(&okV, 16);
        const uint pid = part.mid(colon + 1, equal - colon - 1).toUInt(&okP, 16);
        const int player = part.mid(equal + 1).toInt(&okN);
        if (!okV || !okP || !okN || vid > 0xFFFF || pid > 0xFFFF || player < 1 || player > Players) return false;
        out.append({ quint16(vid), quint16(pid), player });
    }
    if (map) *map = out;
    return true;
}

// ----------------------------------------------------------------- dongles / dongle
quint16 Settings::dongleVid() const
{
    bool ok = false;
    const uint v = m_ini.value(kDongleVid, QStringLiteral("303A")).toString().toUInt(&ok, 16);
    return (ok && v && v <= 0xFFFF) ? quint16(v) : quint16(OF_DONGLE_VID);
}

quint16 Settings::donglePid() const
{
    bool ok = false;
    const uint v = m_ini.value(kDonglePid, QStringLiteral("1001")).toString().toUInt(&ok, 16);
    return (ok && v && v <= 0xFFFF) ? quint16(v) : quint16(OF_DONGLE_PID);
}

void Settings::setDongleVidPid(quint16 vid, quint16 pid)
{
    m_ini.setValue(kDongleVid, QStringLiteral("%1").arg(vid, 4, 16, QLatin1Char('0')).toUpper());
    m_ini.setValue(kDonglePid, QStringLiteral("%1").arg(pid, 4, 16, QLatin1Char('0')).toUpper());
}

// ----------------------------------------------------------------- threads / thread
ThreadPriority Settings::serialPriority() const
{
    return priorityFrom(m_ini.value(kSerialPriority).toString(), ThreadPriority::AboveNormal);
}

ThreadPriority Settings::udpPriority() const
{
    return priorityFrom(m_ini.value(kUdpPriority).toString(), ThreadPriority::Normal);
}

ThreadPriority Settings::encoderPriority() const
{
    return priorityFrom(m_ini.value(kEncoderPriority).toString(), ThreadPriority::Normal);
}

EncoderConfig Settings::encoderConfig() const
{
    // EN: a value missing, not a number or out of range keeps the default. / IT: un valore mancante, non numerico o fuori limite tiene il predefinito.
    const EncoderConfig defaults;
    EncoderConfig c;
    for (const EncoderKey &k : kEncoderDoubles) {
        bool ok = false;
        const double v = m_ini.value(QLatin1String(k.key)).toDouble(&ok);
        if (ok && v >= 0.0 && v < 1e9) c.*k.value = v;
    }
    for (const EncoderIntKey &k : kEncoderInts) {
        bool ok = false;
        const int v = m_ini.value(QLatin1String(k.key)).toInt(&ok);
        if (ok && v > 0) c.*k.value = v;
    }
    if (c.motionHigh <= c.motionLow) {
        c.motionLow = defaults.motionLow;
        c.motionHigh = defaults.motionHigh;
    }
    c.profile30 = qBound(2000, c.profile30, 21000);   // EN: 15 slices of 1,412 bytes at most / IT: al massimo 15 fasce da 1.412 byte
    c.profile60 = qBound(2000, c.profile60, 21000);
    c.qLow = qBound(1, c.qLow, 100);
    c.qHigh = qBound(c.qLow, c.qHigh, 100);
    if (c.cpi <= 0.0) c.cpi = defaults.cpi;
    if (c.maxCycleMs <= 0.0) c.maxCycleMs = defaults.maxCycleMs;
    return c;
}

// ----------------------------------------------------------------- log / registro
bool Settings::logToFile() const
{
    if (!m_logFileOverride.isEmpty()) return true;
    return m_ini.value(kLogToFile, false).toBool();
}

void Settings::setLogToFile(bool on)
{
    m_logFileOverride.clear();
    m_ini.setValue(kLogToFile, on);
}

QString Settings::logFilePath() const
{
    if (!m_logFileOverride.isEmpty()) return m_logFileOverride;
    const QString path = m_ini.value(kLogFile).toString();
    return path.isEmpty() ? defaultLogFilePath() : path;
}

QString Settings::defaultLogFilePath() const
{
    // EN: next to the INI file. / IT: accanto al file INI.
    return QFileInfo(m_ini.fileName()).absolutePath() + QStringLiteral("/OpenFIRE-Scope-Daemon.log");
}

// ----------------------------------------------------------------- window / finestra
QByteArray Settings::windowGeometry() const
{
    return m_ini.value(kWindowGeometry).toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &geometry)
{
    m_ini.setValue(kWindowGeometry, geometry);
}

// ----------------------------------------------------------------- overrides / valori della riga di comando
void Settings::overrideDiscoveryPort(quint16 port)
{
    m_discoveryOverride = port;
}

void Settings::overrideCaptureOff()
{
    m_captureOffOverride = true;
}

void Settings::overrideLogFile(const QString &path)
{
    m_logFileOverride = path;
}

bool Settings::hasOverrides() const
{
    return m_discoveryOverride || m_captureOffOverride || !m_logFileOverride.isEmpty();
}

void Settings::notifyChanged()
{
    m_ini.sync();
    emit changed();
}
