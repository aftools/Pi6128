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
#include "storage.h"

// ============================================================
//  Parser immagini .dsk (formato CPCEMU standard) + directory AMSDOS
//
//  LIMITAZIONI CONOSCIUTE (volute, per tenere il codice affidabile
//  e verificato):
//    - Solo formato DSK STANDARD (non Extended DSK a lunghezza
//      variabile). Extended DSK viene rilevato e rifiutato con un
//      messaggio chiaro invece di essere letto in modo scorretto.
//    - Solo dischi a 1 lato.
//    - Solo formato AMSDOS "Data" (settori R = 0xC1..0xC9, 9 settori
//      da 512 byte a traccia, directory nei primi 4 settori della
//      traccia 0, blocchi da 1KB). E' il formato standard usato dalla
//      stragrande maggioranza dei giochi commerciali single-load/disk
//      (verificato byte-per-byte su un esempio). Il formato "System"
//      (boot CP/M, settori R=0x41..0x49 su traccia 0/1) non e'
//      gestito: se necessario in futuro va aggiunto come caso
//      separato, non e' detto usi la stessa disposizione blocchi.
// ============================================================

#define DSK_MAX_TRACKS            42
#define DSK_MAX_SECTORS_PER_TRACK 12
#define DSK_DIR_ENTRIES           64
#define DSK_MAX_BLOCKS_PER_FILE   128   // fino a 128KB per file (ampio margine)
#define DSK_MAX_LISTED_FILES      32

struct DskSector {
    uint8_t  R;
    uint32_t fileOffset;
    uint16_t size;
};

struct DskTrack {
    uint8_t   numSectors;
    DskSector sectors[DSK_MAX_SECTORS_PER_TRACK];
};

struct AmsdosEntry {
    bool     valid = false;
    char     name[9]  = {0};   // senza spazi, null-terminated
    char     ext[4]   = {0};
    uint8_t  user      = 0;
    uint32_t sizeBytes = 0;
    uint16_t numBlocks = 0;
    uint16_t blocks[DSK_MAX_BLOCKS_PER_FILE];
};

class DskImage {
public:
    bool mount(const char* path);
    bool isMounted() const { return _mounted; }

    // Ricerca case-insensitive, name/ext senza spazi (es. "PACLAND","BIN").
    bool findFile(const char* name, const char* ext, AmsdosEntry& out);

    // Legge fino a 'len' byte del file (offset 0-based, header incluso
    // se presente). Ritorna quanti byte sono stati letti davvero.
    uint32_t readFile(const AmsdosEntry& entry, uint32_t offset, uint8_t* dest, uint32_t len);

    // Elenca i file (un solo entry per file, extent 0). Utile per
    // l'euristica di autorun e per debug/futura selezione a menu.
    int listFiles(AmsdosEntry* outArray, int maxEntries);

private:
    fs::File _file;
    bool     _mounted   = false;
    uint8_t  _numTracks = 0;
    uint8_t  _numSides  = 1;
    DskTrack _tracks[DSK_MAX_TRACKS];
    uint8_t  _rawDir[2048];

    bool     readTrackInfo(uint8_t track, DskTrack& out, uint32_t trackFileOffset);
    bool     readSectorByR(uint8_t track, uint8_t R, uint8_t* dest, uint16_t maxLen);
    uint16_t blockSizeBytes() const { return 1024; }
};

extern DskImage currentDsk;
