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
#include "Z80.h"
#include "memory.h"
#include "dsk.h"

// ============================================================
//  HLE (High-Level Emulation) delle routine firmware CAS
//
//  Invece di emulare l'FDC (uPD765) o richiedere la ROM AMSDOS
//  (protetta da copyright), intercetto le chiamate ai vettori
//  fissi e pubblicamente documentati del firmware CPC (identici per
//  ROM OS/BASIC standard, vedi CPCWiki "BIOS Cassette Functions")
//  e le eseguo direttamente in C++, leggendo i dati dal .dsk
//  montato invece che da un vero registratore o floppy.
//
//  Questo e' lo stesso principio usato internamente da CPCEMU per il
//  suo "bypass" del caricamento cassetta/disco.
//
//  Convenzioni verificate (CY=1 = successo, salvo dove indicato):
//
//   &BC77 CAS_IN_OPEN    in: HL=nome file, B=lunghezza nome, DE=buffer 2K
//                        out: HL=indirizzo header, DE=indirizzo di
//                             caricamento, BC=dimensione, A=tipo file,
//                             CY=1 se aperto con successo
//   &BC7A CAS_IN_CLOSE   out: CY=1 se successo
//   &BC7D CAS_IN_ABANDON out: A=FF, Z=1
//   &BC80 CAS_IN_CHAR    out: A=carattere, CY=1 se successo
//   &BC83 CAS_IN_DIRECT  in: HL=indirizzo destinazione
//                        out: HL=entry point, CY=1 se successo
//   &BC86 CAS_RETURN     (rimette indietro un carattere)
//   &BC89 CAS_TEST_EOF   out: CY=1 se ci sono ancora dati, CY=0 se EOF
//
//  LIMITAZIONE NOTA: gestisce un solo file aperto alla volta (come
//  molti dei loader CPC, che aprono/chiudono in
//  sequenza). Se un gioco aprisse due file contemporaneamente andrebbe
//  esteso con un piccolo array di handle invece di uno singolo.
// ============================================================

#define ADDR_CAS_IN_OPEN     0xBC77
#define ADDR_CAS_IN_CLOSE    0xBC7A
#define ADDR_CAS_IN_ABANDON  0xBC7D
#define ADDR_CAS_IN_CHAR     0xBC80
#define ADDR_CAS_IN_DIRECT   0xBC83
#define ADDR_CAS_RETURN      0xBC86
#define ADDR_CAS_TEST_EOF    0xBC89

struct HleOpenFile {
    bool        open      = false;
    AmsdosEntry entry;
    uint32_t    readPos   = 0;
    uint16_t    entryAddr = 0;
};

static HleOpenFile hleFile;

inline bool isCasTrapAddress(uint16_t pc) {
    switch (pc) {
        case ADDR_CAS_IN_OPEN:
        case ADDR_CAS_IN_CLOSE:
        case ADDR_CAS_IN_ABANDON:
        case ADDR_CAS_IN_CHAR:
        case ADDR_CAS_IN_DIRECT:
        case ADDR_CAS_RETURN:
        case ADDR_CAS_TEST_EOF:
            return true;
        default:
            return false;
    }
}

// Simula il RET dell'istruzione trappata: preleva l'indirizzo di
// ritorno dallo stack (dove si trova perche' e' stato lo Z80 stesso
// a eseguire il CALL originale) e sposta il PC li'.
inline void hleDoRet(Z80& z80) {
    uint16_t sp  = z80.getSP();
    uint16_t ret = memRead(sp) | (memRead(sp + 1) << 8);
    z80.setSP(sp + 2);
    z80.setPC(ret);
}

inline void hleSetCarry(Z80& z80, bool set) {
    uint8_t f = z80.getF();
    if (set) f |= FLAG_C; else f &= ~FLAG_C;
    z80.setAF((z80.getA() << 8) | f);
}

inline void hleSetA(Z80& z80, uint8_t a) {
    z80.setAF((a << 8) | z80.getF());
}

// Legge B byte a partire da HL come nome file grezzo (come scritto
// dal programma in memoria), lo splitta in nome (8)
// ed estensione (3), tutto maiuscolo, senza spazi.
inline void hleReadFilename(Z80& z80, char* outName, char* outExt) {
    uint16_t addr = z80.getHL();
    uint8_t  len  = (uint8_t)(z80.getBC() >> 8);   // B
    char raw[13];
    uint8_t n = (uint8_t)min((int)len, 12);
    for (uint8_t i = 0; i < n; i++) raw[i] = (char)memRead(addr + i);
    raw[n] = 0;

    int ni = 0, ei = 0;
    bool inExt = false;
    for (int i = 0; raw[i] && i < 12; i++) {
        char c = toupper((unsigned char)raw[i]);
        if (c == '.') { inExt = true; continue; }
        if (c == ' ' || c == 0) continue;
        if (inExt) { if (ei < 3) outExt[ei++] = c; }
        else       { if (ni < 8) outName[ni++] = c; }
    }
    outName[ni] = 0;
    outExt[ei]  = 0;
}

// ---- CAS_IN_OPEN (&BC77) ----
inline void hleCasInOpen(Z80& z80) {
    char name[9] = {0}, ext[4] = {0};
    hleReadFilename(z80, name, ext);
    uint16_t workbuf = z80.getDE();

    Serial.printf("[HLE] CAS_IN_OPEN \"%s.%s\" (workbuf=%04X)\n", name, ext, workbuf);

    AmsdosEntry entry;
    if (!currentDsk.isMounted() || !currentDsk.findFile(name, ext, entry)) {
        Serial.println("[HLE]   -> file non trovato");
        hleFile.open = false;
        hleSetCarry(z80, false);
        hleDoRet(z80);
        return;
    }

    hleFile.open    = true;
    hleFile.entry   = entry;
    hleFile.readPos = 0;

    // Legge i primi 128 byte per verificare se c'e' un header AMSDOS
    // valido (checksum), altrimenti tratta il file come headerless.
    uint8_t hdr[128] = {0};
    uint32_t gotHdr = currentDsk.readFile(entry, 0, hdr, min((uint32_t)128, entry.sizeBytes));

    uint16_t checksum = 0;
    for (int i = 0; i < 67; i++) checksum += hdr[i];
    bool validHeader = (gotHdr >= 69) && ((hdr[67] | (hdr[68] << 8)) == checksum);

    uint8_t  fileType;
    uint16_t loadAddr, entrySize;

    if (validHeader) {
        fileType         = hdr[18];
        loadAddr         = hdr[21] | (hdr[22] << 8);
        hleFile.entryAddr = hdr[26] | (hdr[27] << 8);
        entrySize         = (uint16_t)min((uint32_t)0xFFFF, entry.sizeBytes - 128);
        hleFile.readPos   = 128;   // i dati veri iniziano dopo l'header

        for (int i = 0; i < 128; i++) memWrite(workbuf + i, hdr[i]);
        z80.setHL(workbuf);
        z80.setDE(loadAddr);
    } else {
        fileType          = 3;     // convenzione locale: "headerless"
        loadAddr           = 0;
        hleFile.entryAddr = 0;
        entrySize          = (uint16_t)min((uint32_t)0xFFFF, entry.sizeBytes);
        hleFile.readPos    = 0;

        z80.setHL(workbuf);
        z80.setDE(0);
    }

    z80.setBC(entrySize);
    hleSetA(z80, fileType);
    hleSetCarry(z80, true);

    Serial.printf("[HLE]   -> trovato, size=%lu, header=%s, type=%d, load=%04X, entry=%04X\n",
                  (unsigned long)entry.sizeBytes, validHeader ? "si" : "no",
                  fileType, loadAddr, hleFile.entryAddr);

    hleDoRet(z80);
}

// ---- CAS_IN_DIRECT (&BC83) ----
inline void hleCasInDirect(Z80& z80) {
    if (!hleFile.open) {
        hleSetCarry(z80, false);
        hleDoRet(z80);
        return;
    }

    uint16_t dest = z80.getHL();
    uint32_t remaining = hleFile.entry.sizeBytes - hleFile.readPos;

    uint8_t buf[512];
    uint32_t total = 0;
    while (remaining > 0) {
        uint32_t chunk = min((uint32_t)sizeof(buf), remaining);
        uint32_t got = currentDsk.readFile(hleFile.entry, hleFile.readPos, buf, chunk);
        if (got == 0) break;
        for (uint32_t i = 0; i < got; i++) memWrite((uint16_t)(dest + total + i), buf[i]);
        hleFile.readPos += got;
        total   += got;
        remaining -= got;
    }

    Serial.printf("[HLE] CAS_IN_DIRECT -> %lu byte scritti a %04X, entry=%04X\n",
                  (unsigned long)total, dest, hleFile.entryAddr);

    z80.setHL(hleFile.entryAddr);
    hleSetCarry(z80, true);
    hleDoRet(z80);
}

// ---- CAS_IN_CLOSE / ABANDON ----
inline void hleCasInClose(Z80& z80) {
    hleFile.open = false;
    hleSetCarry(z80, true);
    hleDoRet(z80);
}

inline void hleCasInAbandon(Z80& z80) {
    hleFile.open = false;
    hleSetA(z80, 0xFF);
    uint8_t f = z80.getF() | FLAG_Z;
    z80.setAF((z80.getA() << 8) | f);
    hleDoRet(z80);
}

// ---- CAS_IN_CHAR (&BC80) ----
inline void hleCasInChar(Z80& z80) {
    if (!hleFile.open || hleFile.readPos >= hleFile.entry.sizeBytes) {
        hleSetCarry(z80, false);
        hleDoRet(z80);
        return;
    }
    uint8_t c = 0;
    currentDsk.readFile(hleFile.entry, hleFile.readPos, &c, 1);
    hleFile.readPos++;
    hleSetA(z80, c);
    hleSetCarry(z80, true);
    hleDoRet(z80);
}

// ---- CAS_RETURN (&BC86) — rimette indietro un carattere ----
inline void hleCasReturn(Z80& z80) {
    if (hleFile.open && hleFile.readPos > 0) hleFile.readPos--;
    hleDoRet(z80);
}

// ---- CAS_TEST_EOF (&BC89) ----
inline void hleCasTestEof(Z80& z80) {
    bool eof = !hleFile.open || hleFile.readPos >= hleFile.entry.sizeBytes;
    hleSetCarry(z80, !eof);
    hleDoRet(z80);
}

// ---- Dispatcher, chiamato dal loop PRIMA di z80.step() ----
// Ritorna true se ha gestito l'istruzione a questo indirizzo (nel
// qual caso z80.step() NON va chiamato in questo giro).
inline bool hleCasIntercept(Z80& z80) {
    switch (z80.getPC()) {
        case ADDR_CAS_IN_OPEN:    hleCasInOpen(z80);    return true;
        case ADDR_CAS_IN_DIRECT:  hleCasInDirect(z80);  return true;
        case ADDR_CAS_IN_CLOSE:   hleCasInClose(z80);   return true;
        case ADDR_CAS_IN_ABANDON: hleCasInAbandon(z80); return true;
        case ADDR_CAS_IN_CHAR:    hleCasInChar(z80);    return true;
        case ADDR_CAS_RETURN:     hleCasReturn(z80);    return true;
        case ADDR_CAS_TEST_EOF:   hleCasTestEof(z80);   return true;
        default: return false;
    }
}
