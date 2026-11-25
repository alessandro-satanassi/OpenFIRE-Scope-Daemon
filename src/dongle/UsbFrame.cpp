#include "dongle/UsbFrame.h"

#include <QList>

#include "OpenFIRE_Scope_Protocol.h"

namespace UsbFrame {

namespace {
QByteArray frameOf(int contentLen)
{
    QByteArray f(OF_USB_HEADER + contentLen, '\0');
    f[0] = char(OF_USB_SYNC_0);
    f[1] = char(OF_USB_SYNC_1);
    f[2] = char(contentLen & 0xFF);
    f[3] = char((contentLen >> 8) & 0xFF);
    return f;
}
} // namespace

QByteArray control(const QByteArray &text)
{
    const int n = int(qMin<qsizetype>(text.size(), OF_USB_TEXT_MAX));
    QByteArray f = frameOf(1 + n);
    f[OF_USB_HEADER] = char(OF_USB_TYPE_CONTROL);
    std::copy(text.constData(), text.constData() + n, f.data() + OF_USB_HEADER + 1);
    return f;
}

QByteArray test(int contentLen)
{
    contentLen = qBound(1, contentLen, OF_USB_MAX_CONTENT);
    QByteArray f = frameOf(contentLen);
    f[OF_USB_HEADER] = char(OF_USB_TYPE_TEST);
    return f;
}

QByteArray video(uint16_t frame, uint8_t slice, uint8_t q, uint8_t player, const uint8_t *data, int len)
{
    len = qBound(0, len, OF_VIDEO_MAX_DATA);
    QByteArray f = frameOf(OF_VIDEO_HEADER + len);
    char *h = f.data() + OF_USB_HEADER;
    h[OF_VIDEO_B_MARKER] = char(OF_VIDEO_MARKER);
    h[OF_VIDEO_B_FLAGS] = char(OF_VIDEO_VERSION << 4);
    h[OF_VIDEO_B_FRAME] = char(frame & 0xFF);
    h[OF_VIDEO_B_FRAME + 1] = char(frame >> 8);
    h[OF_VIDEO_B_SLICE] = char(slice);
    h[OF_VIDEO_B_Q] = char(q);
    h[OF_VIDEO_B_PLAYER] = char(player);
    h[OF_VIDEO_B_SESSION] = 0;
    if (len > 0) std::copy(data, data + len, h + OF_VIDEO_HEADER);
    return f;
}

void setVideoFrameNumber(QByteArray &frame, uint16_t number)
{
    if (frame.size() < OF_USB_HEADER + OF_VIDEO_HEADER) return;
    frame[OF_USB_HEADER + OF_VIDEO_B_FRAME] = char(number & 0xFF);
    frame[OF_USB_HEADER + OF_VIDEO_B_FRAME + 1] = char(number >> 8);
}

} // namespace UsbFrame

namespace {
// EN: "KEY=value" fields after the first ';'. / IT: campi "CHIAVE=valore" dopo il primo ';'.
QByteArray field(const QList<QByteArray> &parts, const char *key)
{
    const QByteArray k = QByteArray(key) + '=';
    for (int i = 1; i < parts.size(); ++i)
        if (parts[i].startsWith(k)) return parts[i].mid(k.size());
    return QByteArray();
}
} // namespace

ScopeMessage ScopeMessage::parse(const QByteArray &text)
{
    ScopeMessage m;
    const QByteArray prefix(OF_MSG_SCOPE);
    if (!text.startsWith(prefix)) return m;
    const QList<QByteArray> parts = text.mid(prefix.size()).trimmed().split(';');
    const QByteArray state = parts.value(0);
    if (state == OF_MSG_BOOT) {
        m.kind = Boot;
    } else if (state == OF_MSG_UNPAIRED) {
        m.kind = Unpaired;
    } else if (state.size() == 2 && state[0] == 'P' && state[1] >= '1' && state[1] <= '4') {
        m.kind = Paired;
        m.player = state[1] - '0';
    } else {
        return m;
    }
    m.firmware = QString::fromLatin1(field(parts, "FW"));
    m.channel = field(parts, "CH").toInt();
    return m;
}

StatMessage StatMessage::parse(const QByteArray &text)
{
    StatMessage s;
    const QByteArray prefix(OF_MSG_STAT);
    if (!text.startsWith(prefix)) return s;
    // EN: prefix "OF_STAT;" -> part 0 is "OF_STAT". / IT: prefisso "OF_STAT;" -> la parte 0 e' "OF_STAT".
    const QList<QByteArray> parts = text.trimmed().split(';');
    bool okR = false, okT = false, okD = false, okQ = false;
    s.rx = field(parts, "RX").toUInt(&okR);
    s.tx = field(parts, "TX").toUInt(&okT);
    s.drop = field(parts, "DROP").toUInt(&okD);
    s.qfull = field(parts, "QFULL").toUInt(&okQ);
    s.valid = okR && okT && okD && okQ;
    return s;
}
