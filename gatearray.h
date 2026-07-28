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
#include <Arduino.h>
#include "memory.h"

// ============================================================
//  GATE ARRAY (40010) + CRTC 6845
//
//  Fonti usate per l'implementazione (verificate esplicitamente):
//    - CPCWiki / Grimware: "The mighty CRTC 6845", "CRTC", "Gate Array"
//    - CAST C6845 datasheet (timing horizontal/vertical, citato da
//      neuro-sys.github.io "Notes on Video Display Units in Amstrad CPC")
//    - Schema di wiring indirizzi CRTC->bus indirizzi del CPC (MA0-9 su
//      A1-A10, RA0-2 su A11-A13, MA12-13 su A14-A15) dalla stessa fonte
//
//  Semplificazione dichiarata: il CRTC qui e' aggiornato una volta per
//  scanline (ogni 64us / 256 T-state a 4MHz), NON dot-per-dot. Questo
//  e' sufficiente per timing verticale, interrupt, indirizzamento video
//  e dimensioni schermo dinamiche (R1/R6/R9/R4/R5/R7/R12/R13 tutti
//  rispettati), ma NON permette trucchi "split raster" a livello di
//  singolo carattere all'interno della riga (servirebbe un CRTC dot-accurate
//  sincronizzato 1:1 col renderer, fuori scope).
// ============================================================

// ---- Registri CRTC (18) -------------------------------------------
static uint8_t crtcReg[18] = {
    63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0, 0, 0, 0, 0
};
static uint8_t crtcSelected = 0;

// ---- Stato "live" del generatore di indirizzi/temporizzazione -----
static uint8_t  crtcCharRow      = 0;      // riga carattere corrente (0..R4)
static uint8_t  crtcRasterLine   = 0;      // riga raster nel carattere (RA, 0..R9)
static bool     crtcInVAdjust    = false;  // fase di "vertical total adjust" (R5)
static uint8_t  crtcVAdjustCount = 0;
static uint16_t crtcMaActive     = 0x3000; // MA latched a inizio frame (da R12/R13)
static uint16_t crtcMaRowStart   = 0x3000; // MA di inizio riga corrente (14 bit)
static volatile bool frameReady = false;
static volatile uint16_t frameMaStart = 0x3000;

static volatile bool    crtcVsyncActive   = false;
static uint8_t           crtcVsyncLineCnt = 0;
static int8_t             crtcVsyncIntDelay = -1; // countdown "2 HSYNC dopo VSYNC" (-1 = inattivo)

// ---- Gate Array -----------------------------------------------------
static volatile uint8_t  gaScreenMode   = 1;      // 0,1,2 (implementati 0 e 1)
static volatile uint8_t  gaPenSelected  = 0;       // 0-15 = pen, 16 = border
static volatile uint8_t  gaInk[17]      = {0};     // colore hw (0-26) per ogni pen + border
static volatile bool     gaIntPending   = false;   // richiesta INT verso lo Z80
static uint8_t            gaHsyncCount   = 0;       // contatore 6 bit (0-51) per interrupt ogni 52 righe

// ============================================================
//  CRTC: selezione registro / lettura-scrittura dati
// ============================================================
inline void crtcSelectReg(uint8_t val) { crtcSelected = val & 0x1F; }
inline void crtcWriteData(uint8_t val) { if (crtcSelected < 18) crtcReg[crtcSelected] = val; }

// NOTA: sull'hardware reale solo alcuni registri sono leggibili
// (a seconda del "tipo" di CRTC installato: 14,15,16,17 sempre; altri
// secondo tipo). Qui permetto la lettura di tutti per compatibilita'
// con piu' software possibile: e' una semplificazione volutamente
// permissiva rispetto all'hardware reale.
inline uint8_t crtcReadData() { return (crtcSelected < 18) ? crtcReg[crtcSelected] : 0xFF; }

// ============================================================
//  GATE ARRAY: scrittura comandi (0x7Fxx)
// ============================================================
inline void gateArrayWrite(uint8_t val) {
    uint8_t func = (val >> 6) & 0x03;
    switch (func) {
        case 0: // PENR - selezione pen
            gaPenSelected = val & 0x1F; // 0-15 pen, 16 (bit4) = border
            break;
        case 1: // INKR - colore per il pen selezionato
            if (gaPenSelected <= 16) {
              gaInk[gaPenSelected] = val & 0x1F;
              //Serial.printf("INK: mode %d, pen=%d, col=%d\n", gaScreenMode, gaPenSelected, val & 0x1F);
            }
            break;
        case 2: // RMR - modo video + banking ROM + controllo interrupt
            gaScreenMode   = val & 0x03;
            lowerRomEnable = !(val & 0x04);
            upperRomEnable = !(val & 0x08);
            if (val & 0x10) gaHsyncCount = 0;   // bit4: reset contatore interrupt (NON richiede INT)
            break;
        case 3: // MMR - RAM management (banking RAM esteso, non implementato: solo 64K base)
            break;
    }
}

// ============================================================
//  Interrupt periodico Gate Array (contatore 6 bit sulle HSYNC)
//  Fonte: CPCWiki/Grimware - "There is an internal 6-bit counter
//  incremented on each HSYNC. At 52 it wraps to 0 and fires INT.
//  After VSYNC 'sensed' (qui: 2 HSYNC dopo l'inizio del VSYNC) il
//  contatore viene azzerato, e se al momento dell'azzeramento il suo
//  bit5 era 0 (cioe' valore <32) viene generato ANCHE un interrupt."
// ============================================================
inline void gaHsyncTick() {
    gaHsyncCount++;
    if (gaHsyncCount >= 52) {
        gaHsyncCount = 0;
        gaIntPending = true;
    }
}

inline void gaVsyncSyncCheck() {
    // Chiamata 2 HSYNC dopo l'inizio del VSYNC (vedi crtcTickScanline)
    if (gaHsyncCount < 32) gaIntPending = true;
    gaHsyncCount = 0;
}

// ============================================================
//  CRTC: inizio di un nuovo frame verticale -> ricarica MA da R12/R13
//  (il datasheet: "ogni volta che la CPU scrive R12/R13, il Linear Address
//  Generator viene aggiornato all'inizio del PROSSIMO frame verticale")
// ============================================================
inline void crtcFrameStart() {
    crtcMaActive   = (((uint16_t)crtcReg[12] & 0x3F) << 8) | crtcReg[13];
    crtcMaRowStart = crtcMaActive;
    crtcCharRow    = 0;
    crtcRasterLine = 0;
}

inline void crtcInit() {
    crtcFrameStart();
    crtcVsyncActive = false;
    crtcVsyncLineCnt = 0;
    crtcVsyncIntDelay = -1;
    crtcInVAdjust = false;
    crtcVAdjustCount = 0;
    gaHsyncCount = 0;
    gaIntPending = false;
}

// ============================================================
//  CRTC: avanzamento di UNA scanline (chiamata dal core0 ogni 64us,
//  cioe' ogni 256 T-state a 4MHz - vedi cpc464_pico.ino)
// ============================================================
inline void crtcTickScanline() {
    gaHsyncTick();

    if (!crtcInVAdjust) {
        crtcRasterLine++;
        if (crtcRasterLine > crtcReg[9]) {
            crtcRasterLine = 0;
            crtcCharRow++;
            crtcMaRowStart = (crtcMaRowStart + crtcReg[1]) & 0x3FFF;

            if (crtcCharRow == crtcReg[7] && !crtcVsyncActive) {
                crtcVsyncActive = true;
                crtcVsyncLineCnt = 0;
                crtcVsyncIntDelay = 2;
            }

            if (crtcCharRow > crtcReg[4]) {
                crtcCharRow = 0;
                crtcRasterLine = 0;
                if (crtcReg[5] > 0) {
                    crtcInVAdjust = true;
                    crtcVAdjustCount = 0;
                } else {
                    crtcFrameStart();
                    frameMaStart = crtcMaRowStart;   // <-- COPIA STABILE
                    frameReady = true;
                }
            }
        }
    } else {
        crtcVAdjustCount++;
        if (crtcVAdjustCount >= crtcReg[5]) {
            crtcInVAdjust = false;
            crtcFrameStart();
            frameMaStart = crtcMaRowStart;           // <-- COPIA STABILE
            frameReady = true;
        }
    }

    // Gestione VSYNC e interrupt (invariato)
    if (crtcVsyncActive) {
        crtcVsyncLineCnt++;
        if (crtcVsyncLineCnt >= 16) crtcVsyncActive = false;
    }

    if (crtcVsyncIntDelay >= 0) {
        if (--crtcVsyncIntDelay < 0) gaVsyncSyncCheck();
    }
}

// ============================================================
//  Indirizzo di memoria video per un dato carattere/scanline,
//  secondo il wiring reale CRTC->bus indirizzi del CPC:
//    A15,A14 <- MA13,MA12  (selezione pagina 16K: *0x4000)
//    A13..A11 <- RA2..RA0  (riga raster nel carattere: *0x800)
//    A10..A1  <- MA9..MA0  (posizione carattere nella riga)
//    A0       <- CCLK      (1o/2o byte del carattere)
// ============================================================
inline uint16_t crtcVideoByteAddress(uint8_t ra, uint8_t charCol, uint8_t byteInChar) {
    uint16_t maChar = (crtcMaRowStart + charCol) & 0x3FFF;
    uint16_t page   = (maChar >> 12) & 0x03;
    uint16_t low10  = maChar & 0x03FF;
    return (page << 14) | ((uint16_t)(ra & 0x07) << 11) | (low10 << 1) | (byteInChar & 1);
}

// Dimensioni schermo correnti secondo i registri CRTC attivi
// (usate dal renderer per adattarsi dinamicamente).
inline uint8_t crtcCharsPerRow()   { return crtcReg[1]; }        // R1 (caratteri/riga)
inline uint8_t crtcCharRows()      { return crtcReg[6]; }        // R6 (righe carattere visibili)
inline uint8_t crtcScanlinesPerCharRow() { return crtcReg[9] + 1; } // R9+1

// ============================================================
//  Palette hardware CPC (swapped)
// ============================================================

static const uint16_t cpcHwPalette[32] = {
    // 0      1      2      3      4      5      6      7
    0x1084, 0x1084, 0xF007, 0xF087, 0x1000, 0x10F0, 0x1004, 0x10F4,
    // 8      9      10     11     12     13     14     15
    0x10F0, 0xF087, 0xE0FF, 0xFFFF, 0x00F8, 0x1FF8, 0x00FC, 0x1FFC,
    // 16     17     18     19     20     21     22     23
    0x1000, 0xF007, 0xE007, 0xFF07, 0x0000, 0x1F00, 0xE003, 0x1F04,
    // 24     25     26     27     28     29     30     31
    0x1060, 0xF077, 0xE077, 0xFF77, 0x0060, 0x1F60, 0xE063, 0x1F64
};

// confrontata con la tabella ufficiale CPCWiki "Colour Palette"; qui e' una
// approssimazione 16bit RGB565 swapped 32 posizioni (indirizzamento a 5 bit: 2^5 = 32 valori, alcuni duplicati e nessuna traduzione ).

inline uint16_t gaColorRGB565(uint8_t penOrBorder) {
    uint8_t hw = gaInk[penOrBorder] & 0x1F;   // 0-31, nessuna modulo/traduzione
    return cpcHwPalette[hw];
}
