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
#include <string.h>
#include <strings.h>
#include "Z80.h"
#include "memory.h"
#include "dsk.h"

// ============================================================
//  Autorun: sceglie euristicamente il file da avviare e lo carica
//  direttamente in RAM, bypassando BASIC/AMSDOS per il file INIZIALE
//  (gli eventuali file caricati a runtime dal gioco stesso passano
//  invece dall'HLE in hle_cas.h).
//
//  Questo funziona SOLO per file di tipo Binary con header AMSDOS
//  valido (il caso di molti dei giochi commerciali
//  con loader binario).
//
//  LIMITAZIONE NOTA E VOLUTA: i file BASIC (tipo 0/1) non vengono
//  avviati automaticamente in questa versione. Per farlo servirebbe
//  conoscere l'esatto indirizzo delle variabili di sistema del
//  firmware che delimitano l'area di testo del programma BASIC
//  (dipende dalla versione ROM) e il formato tokenizzato preciso:
//  preferisco lasciarlo esplicitamente non implementato piuttosto
//  che inserire indirizzi non verificati che potrebbero corrompere
//  la RAM silenziosamente. 
// ============================================================

inline bool autorunPickBootFile(AmsdosEntry& out) {
    AmsdosEntry list[DSK_MAX_LISTED_FILES];
    int n = currentDsk.listFiles(list, DSK_MAX_LISTED_FILES);
    if (n == 0) return false;

    // 1) DISC.BAS o DISC.BIN (convenzione classica di autoboot su CPC)
    for (int i = 0; i < n; i++) {
        if (strcasecmp(list[i].name, "DISC") == 0 &&
            (strcasecmp(list[i].ext, "BAS") == 0 || strcasecmp(list[i].ext, "BIN") == 0)) {
            return currentDsk.findFile(list[i].name, list[i].ext, out);
        }
    }

    // 2) Unico file .BIN (il caso piu' comune per giochi con loader binario)
    int binIdx = -1, binCount = 0;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(list[i].ext, "BIN") == 0) { binIdx = i; binCount++; }
    }
    if (binCount == 1) return currentDsk.findFile(list[binIdx].name, list[binIdx].ext, out);

    // 3) Unico file .BAS (non avviabile automaticamente)
    int basIdx = -1, basCount = 0;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(list[i].ext, "BAS") == 0) { basIdx = i; basCount++; }
    }
    if (basCount == 1) return currentDsk.findFile(list[basIdx].name, list[basIdx].ext, out);

    // 4) Fallback: primo file valido trovato
    return currentDsk.findFile(list[0].name, list[0].ext, out);
}

inline bool autorunBoot(Z80& z80) {
    AmsdosEntry entry;
    if (!autorunPickBootFile(entry)) {
        Serial.println("[Autorun] Nessun file avviabile trovato sul disco");
        return false;
    }

    Serial.printf("[Autorun] File selezionato: %s.%s (%lu byte)\n",
                  entry.name, entry.ext, (unsigned long)entry.sizeBytes);

    uint8_t hdr[128] = {0};
    uint32_t gotHdr = currentDsk.readFile(entry, 0, hdr, min((uint32_t)128, entry.sizeBytes));

    uint16_t checksum = 0;
    for (int i = 0; i < 67; i++) checksum += hdr[i];
    bool validHeader = (gotHdr >= 69) && ((hdr[67] | (hdr[68] << 8)) == checksum);

    if (!validHeader) {
        Serial.println("[Autorun] Nessun header AMSDOS valido: avvio automatico non possibile per questo file");
        return false;
    }

    uint8_t  type      = hdr[18];
    uint16_t loadAddr  = hdr[21] | (hdr[22] << 8);
    uint16_t entryAddr = hdr[26] | (hdr[27] << 8);

    if (type != 2) {   // 0/1 = Basic, 2 = Binary
        Serial.printf("[Autorun] Tipo file %d (BASIC) non gestito automaticamente in questa versione\n", type);
        return false;
    }

    uint32_t len = entry.sizeBytes - 128;
    uint8_t buf[512];
    uint32_t done = 0;
    while (done < len) {
        uint32_t chunk = min((uint32_t)sizeof(buf), len - done);
        uint32_t got = currentDsk.readFile(entry, 128 + done, buf, chunk);
        if (got == 0) break;
        for (uint32_t i = 0; i < got; i++) memWrite((uint16_t)(loadAddr + done + i), buf[i]);
        done += got;
    }

    Serial.printf("[Autorun] Caricati %lu byte a %04X, salto a entry point %04X\n",
                  (unsigned long)done, loadAddr, entryAddr ? entryAddr : loadAddr);

    z80.setPC(entryAddr ? entryAddr : loadAddr);
    z80.setSP(0xC000);   // stack ragionevole sotto l'area schermo; adatta se il gioco si aspetta altro

    return true;
}
