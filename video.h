/*
 * Questo file fa parte del progetto Pi464.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */
#pragma once
#include <TFT_eSPI.h>
#include "config.h"
#include "memory.h"
#include "gatearray.h"
#include "perf.h"

extern volatile bool firmwareReady;
extern TFT_eSPI tft;

// ============================================================
//  CONFIGURAZIONE RENDER
// ============================================================
// Numero di scanline raggruppate in un unico blocco/trasferimento DMA.
//   1   = comportamento prima versione (una riga alla volta) e con 
//         doppio buffering.
//   8   = una char-row (default consigliato): buon compromesso
//         RAM/overhead, 25 trasferimenti/frame invece di 200.
//   200 = frame intero in un solo pushPixelsDMA. ATTENZIONE RAM:
//         320*200*2 byte = 125.000 byte; il doppio buffer (necessario
//         per pipeline CPU/DMA) ne richiederebbe il doppio (~244KB),
//         che sommato ai 64K di RAM CPC emulata + stack + resto NON
//         entra in un Pico RP2040 base (264KB totali). Con questo
//         valore si perde quindi il pipelining: vedi nota su
//         VIDEO_SINGLE_BUFFER piu' sotto se vuoi comunque provarlo.
#ifndef VIDEO_BLOCK_LINES
#define VIDEO_BLOCK_LINES 8
#endif

// Se 1, usa UN SOLO buffer (niente ping-pong): utile solo se
// VIDEO_BLOCK_LINES e' grande (es. frame intero) e la RAM non
// permette il doppio buffer. 
// Si aspetta sempre la fine del DMA prima di riscrivere, ma annulla
// la sovrapposizione CPU-decode / DMA-transfer.
#ifndef VIDEO_SINGLE_BUFFER
#define VIDEO_SINGLE_BUFFER 0
#endif

// Frameskip dedicato SOLO all'update del display. Non tocca la
// CPU ne' l'audio, che continuano a girare al loro timing reale su
// core0: qui si salta soltanto il pushPixelsDMA di alcuni frame.
// 0 = nessuno skip. N = disegna 1 frame ogni N+1 (es. 1 = dimezza
// il framerate video).
#ifndef VIDEO_FRAME_SKIP
#define VIDEO_FRAME_SKIP 1
#endif

#if VIDEO_SINGLE_BUFFER
static uint16_t blockBuf[1][CPC_SCREEN_W * VIDEO_BLOCK_LINES];
#else
static uint16_t blockBuf[2][CPC_SCREEN_W * VIDEO_BLOCK_LINES];
#endif
static uint8_t  blockBufIdx = 0;

inline void videoInit() {
    tft.init();
    tft.initDMA();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(false);
}

// --- Decoder pixel  ---

inline void decodeMode1Byte(uint8_t b, uint16_t *dst) {
    uint8_t p0 = ((b >> 3) & 1) << 1 | ((b >> 7) & 1);
    uint8_t p1 = ((b >> 2) & 1) << 1 | ((b >> 6) & 1);
    uint8_t p2 = ((b >> 1) & 1) << 1 | ((b >> 5) & 1);
    uint8_t p3 = ((b >> 0) & 1) << 1 | ((b >> 4) & 1);
    dst[0] = gaColorRGB565(p0);
    dst[1] = gaColorRGB565(p1);
    dst[2] = gaColorRGB565(p2);
    dst[3] = gaColorRGB565(p3);
}

inline void decodeMode0Byte(uint8_t b, uint16_t *dst) {
    uint8_t p0 = ((b>>1)&1)<<3 | ((b>>5)&1)<<2 | ((b>>3)&1)<<1 | ((b>>7)&1);
    uint8_t p1 = ((b>>0)&1)<<3 | ((b>>4)&1)<<2 | ((b>>2)&1)<<1 | ((b>>6)&1);
    uint16_t c0 = gaColorRGB565(p0);
    uint16_t c1 = gaColorRGB565(p1);
    dst[0] = c0; dst[1] = c0; dst[2] = c1; dst[3] = c1;
}

// ============================================================
//  Render "a blocchi" con vero doppio buffering (o singolo, vedi
//  VIDEO_SINGLE_BUFFER) e frameskip opzionale dedicato al video.
// ============================================================
inline void renderBufferedFrame() {
    if (!firmwareReady) return;
    if (!frameReady) return;
    frameReady = false;               // consuma il frame in ogni caso

#if VIDEO_FRAME_SKIP > 0
    static uint8_t skipCounter = 0;
    if (skipCounter < VIDEO_FRAME_SKIP) {
        skipCounter++;
        perf.videoSkipped++;
        return;                       // frame consumato ma non disegnato
    }
    skipCounter = 0;
#endif

    uint32_t tStart = micros();

    // ---- copia locale parametri CRTC  ----
    uint8_t  charsPerRow = crtcReg[1];
    uint8_t  charRows    = crtcReg[6];
    uint8_t  scanPerRow  = crtcReg[9] + 1;
    uint16_t maRowStart  = frameMaStart;
    uint8_t  mode        = gaScreenMode;

    int screenW = min((int)charsPerRow * 8, (int)CPC_SCREEN_W);
    int screenH = min((int)charRows * scanPerRow, (int)CPC_SCREEN_H);
    int visibleChars = screenW / 8;

    int displayW = tft.width();
    int displayH = tft.height();
    int xOff = (displayW - screenW) / 2;
    int yOff = (displayH - screenH) / 2;

    tft.startWrite();
    tft.setAddrWindow(xOff, yOff, screenW, screenH);

    int line = 0;
    for (int charRow = 0; charRow < charRows && line < screenH; charRow++) {
        uint16_t rowBase = (maRowStart + charRow * charsPerRow) & 0x3FFF;

        for (int ra = 0; ra < scanPerRow && line < screenH; ra++, line++) {

            // Inizio di un nuovo blocco: aspetta che buffer
            // abbia finito il trasferimento precedente prima di
            // sovrascriverlo. Se il DMA ha gia' finito, dmaWait()
            // ritorna subito: nessun costo quando il video tiene il passo.
            if ((line % VIDEO_BLOCK_LINES) == 0) {
                tft.dmaWait();
            }

            uint16_t *dst = blockBuf[blockBufIdx] + (line % VIDEO_BLOCK_LINES) * screenW;

            for (int c = 0; c < visibleChars; c++) {
                uint16_t maChar = (rowBase + c) & 0x3FFF;
                uint16_t page   = (maChar >> 12) & 0x03;
                uint16_t low10  = maChar & 0x03FF;
                uint16_t addr0  = (page << 14) | ((uint16_t)(ra & 0x07) << 11) | (low10 << 1);
                uint16_t addr1  = addr0 | 1;

                if (mode == 1) {
                    decodeMode1Byte(ram[addr0], dst + c * 8);
                    decodeMode1Byte(ram[addr1], dst + c * 8 + 4);
                } else {
                    decodeMode0Byte(ram[addr0], dst + c * 8);
                    decodeMode0Byte(ram[addr1], dst + c * 8 + 4);
                }
            }

            bool blockFull = ((line + 1) % VIDEO_BLOCK_LINES) == 0;
            bool lastLine  = (line + 1) == screenH;
            if (blockFull || lastLine) {
                int linesInBlock = (line % VIDEO_BLOCK_LINES) + 1;
                tft.pushPixelsDMA(blockBuf[blockBufIdx], screenW * linesInBlock);
                #if !VIDEO_SINGLE_BUFFER
                    blockBufIdx ^= 1;      // ping-pong: il prossimo blocco viene decodificato nell'altro buffer MENTRE questo e' in DMA
                #else
                    tft.dmaWait();         // singolo buffer: serializza
                #endif
            }
        }
    }

    tft.dmaWait();
    tft.endWrite();

    perf.videoBusyUs += micros() - tStart;
    perf.videoFrames++;
}

// Alias per non dover toccare loop1() nel .ino
inline void renderFrame() { renderBufferedFrame(); }