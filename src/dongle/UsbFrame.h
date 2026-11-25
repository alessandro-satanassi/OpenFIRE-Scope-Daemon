#ifndef OFD_USBFRAME_H
#define OFD_USBFRAME_H

#include <QByteArray>
#include <QString>
#include <algorithm>
#include <cstdint>

// EN: USB frame PC <-> dongle: A5 5A, length (16 bit LE, content only), content. Pure code, no I/O.
// IT: Trama USB PC <-> dongle: A5 5A, lunghezza (16 bit LE, solo contenuto), contenuto. Codice puro, niente I/O.
namespace UsbFrame {

// EN: 'C' + text. / IT: 'C' + testo.
QByteArray control(const QByteArray &text);
// EN: 'T' bandwidth test of contentLen bytes (type included). / IT: prova di banda 'T' di contentLen byte (tipo compreso).
QByteArray test(int contentLen);
// EN: 'M' video packet: header of 8 bytes + data (max 1400). Session 0: the dongle writes it.
// IT: pacchetto video 'M': intestazione di 8 byte + dati (max 1400). Sessione 0: la scrive il dongle.
QByteArray video(uint16_t frame, uint8_t slice, uint8_t q, uint8_t player, const uint8_t *data, int len);
// EN: writes the frame number into a video frame built by video(). / IT: scrive il numero di frame in una trama video fatta da video().
void setVideoFrameNumber(QByteArray &frame, uint16_t number);

// EN: Parser with the same states as the one of the dongle. feed() calls onContent for every complete content.
//     A frame left half way for 100 ms goes back to the sync search (checked at every feed and by idle()).
// IT: Parser con gli stessi stati di quello del dongle. feed() chiama onContent per ogni contenuto completo.
//     Una trama rimasta a meta' per 100 ms torna alla ricerca del sincronismo (controllato a ogni feed e da idle()).
class Parser
{
public:
    template <typename Fn>
    void feed(const uint8_t *data, int len, qint64 nowMs, Fn &&onContent)
    {
        if (len <= 0) return;
        idle(nowMs);
        m_lastByteMs = nowMs;
        for (int i = 0; i < len; ++i) {
            const uint8_t b = data[i];
            switch (m_state) {
            case WaitSync0: if (b == 0xA5) m_state = WaitSync1; break;
            case WaitSync1: m_state = (b == 0x5A) ? WaitLen0 : (b == 0xA5 ? WaitSync1 : WaitSync0); break;
            case WaitLen0:  m_len = b; m_state = WaitLen1; break;
            case WaitLen1:
                m_len = uint16_t(m_len | (uint16_t(b) << 8));
                m_pos = 0;
                m_state = (m_len == 0 || m_len > MaxContent) ? WaitSync0 : WaitData;
                break;
            case WaitData: {
                if (m_pos == 0 && !typeValid(b)) {
                    m_state = (b == 0xA5) ? WaitSync1 : WaitSync0;
                    ++m_discarded;
                    break;
                }
                int take = m_len - m_pos;
                if (take > len - i) take = len - i;
                std::copy(data + i, data + i + take, m_content + m_pos);
                m_pos = uint16_t(m_pos + take);
                i += take - 1;
                if (m_pos == m_len) {
                    onContent(m_content, int(m_len));
                    m_state = WaitSync0;
                }
                break;
            }
            }
        }
    }
    void idle(qint64 nowMs)
    {
        if (m_state != WaitSync0 && nowMs - m_lastByteMs >= ResyncMs) {
            m_state = WaitSync0;
            ++m_discarded;
        }
    }
    void reset() { m_state = WaitSync0; }
    int discarded() const { return m_discarded; }

    static constexpr int MaxContent = 1408;
    static constexpr int ResyncMs = 100;

private:
    static bool typeValid(uint8_t t) { return t == 0x4D || t == 0x43 || t == 0x54; }
    enum State { WaitSync0, WaitSync1, WaitLen0, WaitLen1, WaitData };
    State m_state = WaitSync0;
    uint16_t m_len = 0;
    uint16_t m_pos = 0;
    qint64 m_lastByteMs = 0;
    int m_discarded = 0;
    uint8_t m_content[MaxContent];
};

} // namespace UsbFrame

// EN: Messages of the dongle. Unknown fields are ignored. / IT: Messaggi del dongle. I campi sconosciuti si ignorano.
struct ScopeMessage {
    enum Kind { Invalid, Boot, Unpaired, Paired };
    Kind kind = Invalid;
    int player = 0;      // 1..4 with Paired / con Paired
    QString firmware;
    int channel = 0;
    static ScopeMessage parse(const QByteArray &text);   // "OF_SCOPE:..."
};

struct StatMessage {
    bool valid = false;
    quint32 rx = 0, tx = 0, drop = 0, qfull = 0;
    static StatMessage parse(const QByteArray &text);    // "OF_STAT;..."
};

#endif // OFD_USBFRAME_H
