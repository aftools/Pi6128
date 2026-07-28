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
#include "AY3891.h"
#include "gatearray.h"   // per crtcVsyncActive
#include "keyboard_matrix.h"  

extern volatile uint8_t keyMatrix[10];
extern AY3891 ay;

// Registri interni del PPI
static uint8_t ppiPortA_out = 0;   // valore scritto su Port A (output)
static uint8_t ppiPortA_in  = 0;   // valore letto da Port A (input, dal PSG)
static uint8_t ppiPortB     = 0;   // valore scritto su Port B (output)
static uint8_t ppiPortC     = 0;   // valore scritto su Port C (output)

inline void applyPsgControl() {
    bool bdir = ppiPortC & 0x80;
    bool bc1  = ppiPortC & 0x40;
    uint8_t sel = (bdir << 1) | bc1;
    switch (sel) {
        case 0: /* inattivo */ break;
        case 1: ppiPortA_in = ay.readData(); break;
        case 2: ay.writeData(ppiPortA_out); break;
        case 3: ay.selectRegister(ppiPortA_out); break;
    }
}


inline uint8_t ppiReadPortB() {
    uint8_t val = 0b01110000;
    if (crtcVsyncActive) val |= 0x01;
    return val;
}

inline void ppiWritePortA(uint8_t val) { 
    ppiPortA_out = val;
    //Serial.printf("PortA<=%02X\n", val);
}

inline void ppiWritePortB(uint8_t val) { 
    ppiPortB = val; 
}

inline void ppiWritePortC(uint8_t val) {
    ppiPortC = val;
    cpcKeyboardRowSel = val & 0x0F;   // riga tastiera selezionata direttamente da PC0-3

    bool bdir = val & 0x80;
    bool bc1  = val & 0x40;
    uint8_t sel = (bdir << 1) | bc1;
    switch (sel) {
        case 0: break;
        case 1: ppiPortA_in = ay.readData(); break;
        case 2: ay.writeData(ppiPortA_out);  break;
        case 3: ay.selectRegister(ppiPortA_out); break;
    }
}


inline uint8_t ppiReadPortA() { 
    return ppiPortA_in;   // restituisce il valore letto dal PSG
}

inline uint8_t ppiReadPortC() {
    // Riusa la stessa riga gia' selezionata via Port C (vedi
    // ppiWritePortC: cpcKeyboardRowSel = val & 0x0F), lo stesso
    // meccanismo che AY3891::readData() usa per il registro 14.
    //
    // NOTA hardware: sulla CPC reale, leggere Port C restituisce lo
    // stato dei pin di INPUT (cassette in, expansion, non dati
    // tastiera): i dati tastiera si leggono da Port A tramite il PSG
    // (reg.14), non da qui. Questa funzione resta come fallback
    if (cpcKeyboardRowSel >= 10) return 0xFF;
    return keyMatrix[cpcKeyboardRowSel];
}

inline void ppiWriteControl(uint8_t val) {
    if (val & 0x80) {
        // Mode Set (es. 0x82) - il firmware CPC usa sempre modo 0, non serve altro
        return;
    }
    // Bit Set/Reset su Port C: bit7=0, bit3-1=numero bit, bit0=set/reset
    uint8_t bitNum = (val >> 1) & 0x07;
    if (val & 0x01) ppiPortC |=  (1 << bitNum);
    else            ppiPortC &= ~(1 << bitNum);

    applyPsgControl();
}
