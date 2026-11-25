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

/*!
 * @file OpenFIRE_Scope_Protocol.h
 * @brief Protocol shared by the PC daemon, the scope dongle and the games
 * @n Protocollo comune a demone PC, dongle del mirino e giochi
 *
 * EN: Plain C header, no dependencies: the daemon, the firmware and a game can include it as it is.
 *     Values from the "OpenFIRE scope video specification" and the "OpenFIRE PC daemon specification"
 *     (24-25 November 2026) and from the scope dongle firmware (OpenFIRE_Scope_Dongle_USB.cpp,
 *     OpenFIRE_Wireless_Scope.h). Keep it identical in every project that copies it.
 * IT: Header C semplice, senza dipendenze: demone, firmware e giochi possono includerlo cosi' com'e'.
 *     Valori dalla "Specifica video del mirino OpenFIRE" e dalla "Specifica del demone PC OpenFIRE"
 *     (24-25 novembre 2026) e dal firmware del dongle del mirino (OpenFIRE_Scope_Dongle_USB.cpp,
 *     OpenFIRE_Wireless_Scope.h). Va tenuto identico in ogni progetto che lo copia.
 *
 * @copyright alessandro-satanassi, https://github.com/alessandro-satanassi, 2026
 * @copyright GNU General Public License v3
 *
 * @author [Alessandro Satanassi](alessandro@cittini.it)
 * @version V1.0
 * @date 2026
 */

#ifndef OPENFIRE_SCOPE_PROTOCOL_H
#define OPENFIRE_SCOPE_PROTOCOL_H

#include <stdint.h>

// ===================================================================================
// IMAGE / IMMAGINE
// ===================================================================================
#define OF_SCOPE_W                  240
#define OF_SCOPE_H                  240
#define OF_SCOPE_STRIPS             15     // strips of 16 rows / fasce da 16 righe
#define OF_SCOPE_STRIP_ROWS         16

// ===================================================================================
// USB FRAME PC <-> DONGLE / TRAMA USB PC <-> DONGLE
// ===================================================================================
// EN: A5 5A, length (16 bit little-endian, content only), content. No CRC: USB already checks the data.
// IT: A5 5A, lunghezza (16 bit little-endian, solo il contenuto), contenuto. Niente CRC: la USB controlla gia' i dati.
#define OF_USB_SYNC_0               0xA5
#define OF_USB_SYNC_1               0x5A
#define OF_USB_HEADER               4      // sync + length / sincronismo + lunghezza
#define OF_USB_TYPE_VIDEO           0x4D   // 'M': video packet / pacchetto video
#define OF_USB_TYPE_CONTROL         0x43   // 'C': ASCII text, no terminator / testo ASCII, senza terminatore
#define OF_USB_TYPE_TEST            0x54   // 'T': bandwidth test, counted in RX and discarded / prova di banda, contata in RX e scartata
#define OF_USB_MAX_CONTENT          1408   // 8 + 1400
#define OF_USB_TEXT_MAX             96     // longest control text of the dongle / testo di controllo piu' lungo del dongle
#define OF_USB_RESYNC_MS            100    // dongle: frame left half way for so long -> back to the sync / dongle: trama a meta' per tanto -> si torna al sincronismo
#define OF_USB_STAT_EVERY_MS        500    // dongle: OF_STAT period / dongle: periodo di OF_STAT

// EN: Control messages, PC -> dongle. / IT: Messaggi di controllo, PC -> dongle.
#define OF_CMD_WHOAMI               "WHOAMI"
#define OF_CMD_START                "START"    // video of the slot starts: the dongle empties its queue, session + 1 / parte il video: il dongle svuota la coda, sessione + 1
#define OF_CMD_PAUSE                "PAUSE"    // game paused, every 200 ms / gioco in pausa, ogni 200 ms
#define OF_CMD_RESCAN               "RESCAN"   // "RESCAN;SKIP=36,40": only to a free dongle / solo a un dongle libero

// EN: Control messages, dongle -> PC. / IT: Messaggi di controllo, dongle -> PC.
#define OF_MSG_SCOPE                "OF_SCOPE:"   // BOOT | UNPAIRED;FW=a.b.c;CH=n | Pn;FW=a.b.c;CH=n
#define OF_MSG_STAT                 "OF_STAT;"    // RX=..;TX=..;DROP=..;QFULL=.. (counters from power on / contatori dall'accensione)
#define OF_MSG_BOOT                 "BOOT"
#define OF_MSG_UNPAIRED             "UNPAIRED"
#define OF_MSG_NONE                 "NONE"        // discovery only: no dongle / solo discovery: nessun dongle

// EN: 5 GHz channels of the dongles, bit i of the SKIP mask = channel i. / IT: canali a 5 GHz dei dongle, bit i della maschera SKIP = canale i.
#define OF_SCOPE_CHANNELS           { 36, 40, 44, 48 }
#define OF_SCOPE_N_CHANNELS         4

// ===================================================================================
// VIDEO PACKET TO THE SCOPE ('M') / PACCHETTO VIDEO AL MIRINO ('M')
// ===================================================================================
#define OF_VIDEO_MARKER             0x4D
#define OF_VIDEO_VERSION            1        // 4 high bits of byte 1 / 4 bit alti del byte 1
#define OF_VIDEO_FLAG_SESSION       0x08     // bit 3: set by the dongle / bit 3: lo mette il dongle
#define OF_VIDEO_HEADER             8
#define OF_VIDEO_MAX_DATA           1400
#define OF_VIDEO_MAX_PACKET         (OF_VIDEO_HEADER + OF_VIDEO_MAX_DATA)
#define OF_VIDEO_PRESENCE           0xFF     // slice index of a presence packet / indice di fascia della presenza

#define OF_VIDEO_B_MARKER           0
#define OF_VIDEO_B_FLAGS            1        // version (high) and flags (low) / versione (alti) e flag (bassi)
#define OF_VIDEO_B_FRAME            2        // 2-3, little-endian
#define OF_VIDEO_B_SLICE            4
#define OF_VIDEO_B_Q                5
#define OF_VIDEO_B_PLAYER           6
#define OF_VIDEO_B_SESSION          7        // written by the dongle / la scrive il dongle

// ===================================================================================
// GAME -> DAEMON, UDP ON 127.0.0.1 ('OF') / GIOCO -> DEMONE, UDP SU 127.0.0.1 ('OF')
// ===================================================================================
#define OF_UDP_DISCOVERY_PORT       47810    // WHOAMI -> one line per dongle / una riga per dongle
#define OF_UDP_VIDEO_PORT_P1        47811    // P2 47812, P3 47813, P4 47814; the game always uses the PORT it received / il gioco usa sempre la PORT ricevuta
#define OF_UDP_VERSION              1
#define OF_UDP_FMT_RGB888           0
#define OF_UDP_FMT_RGB565           1        // little-endian
#define OF_UDP_FMT_BGRX8888         2        // 4th byte ignored / quarto byte ignorato
#define OF_UDP_STRIP_PRESENCE       0xFF     // header only: game paused / solo intestazione: gioco in pausa
#define OF_UDP_HEADER               16

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[2];     /* 'O', 'F' */
    uint8_t  version;      /* 1 */
    uint8_t  format;       /* 0 RGB888, 1 RGB565 LE, 2 BGRX8888 */
    uint32_t frame;        /* little-endian, per player / per giocatore */
    uint8_t  strip;        /* 0..14, 0xFF = presence / presenza */
    uint8_t  strips;       /* 15 */
    uint8_t  player;       /* 1..4 */
    uint32_t session;      /* little-endian, random != 0 at every game start / casuale != 0 a ogni avvio del gioco */
    uint8_t  reserved;
    /* follows: 240 x 16 pixels (none for a presence) / segue: 240 x 16 pixel (niente per la presenza) */
} of_frame_hdr_t;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(of_frame_hdr_t) == OF_UDP_HEADER, "of_frame_hdr_t must be 16 bytes");
#endif

// ===================================================================================
// USB IDENTITIES / IDENTITA' USB
// ===================================================================================
#define OF_DONGLE_VID               0x303A   // ESP32-C5 USB Serial/JTAG, fixed in silicon / fisso nel silicio
#define OF_DONGLE_PID               0x1001
#define OF_LIGHTGUN_VID             0xF143   // OpenFIRE lightgun (OpenFIREDefines.h DEVICE_VID); PID = player 1..4 by default / PID = giocatore 1..4 di default

#endif // OPENFIRE_SCOPE_PROTOCOL_H
