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
#include "cpc464rom.h"
#include "amsdosrom.h"
// RAM fisica 64K (sempre presente, anche sotto la ROM: le scritture
// vanno sempre in RAM, le letture vengono "coperte" dalla ROM se abilitata)
static uint8_t ram[65536];

// Stato banking, controllato dal Gate Array (funzione 0xC0, vedi gatearray.h)
static volatile bool lowerRomEnable = true;   // 0x0000-0x3FFF
static volatile bool upperRomEnable = true;   // 0xC000-0xFFFF

// Numero di ROM alta selezionata tramite la porta &DFxx
// (bit13=0, sola scrittura). Convenzione standard CPC: 0=BASIC,
// 7=AMSDOS (vedi cpctech.cpcwiki.de/docs/exprom.html). Qualunque
// numero non gestito ricade su BASIC, come su hardware reale quando
// si seleziona una ROM non collegata.
static volatile uint8_t upperRomSelect = 0;															   
inline void memoryInit() {
    memset(ram, 0, sizeof(ram));
}

// Da agganciare alla decodifica I/O per la porta &DFxx (bit13==0),
// sola scrittura: vedi istruzioni di integrazione nel .ino.
inline void romSelectWrite(uint8_t val) {
    upperRomSelect = val;
}																   
// ---- Callback per la classe Z80 ----
inline uint8_t memRead(uint16_t addr) {
    if (addr < 0x4000 && lowerRomEnable) {
        return cpc464rom[addr];                 // OS ROM (primi 16K dell'immagine)
    }
    if (addr >= 0xC000 && upperRomEnable) {
        if (upperRomSelect == 7) {
            return amsdosRom[addr - 0xC000];
        }					  
        return cpc464rom[0x4000 + (addr - 0xC000)]; // BASIC ROM (16K successivi)
    }
    return ram[addr];
}

inline void memWrite(uint16_t addr, uint8_t data) {
    // La RAM e' sempre scrivibile, anche se coperta dalla ROM in lettura
    ram[addr] = data;
}
