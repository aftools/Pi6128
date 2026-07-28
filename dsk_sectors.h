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
//  DskImage2 — FDC reale + AMSDOS.
//
//  Sul Pico non posso caricare l'intero disco in memoria
//  (dischi anche di 200KB+ a fronte di ~264KB di SRAM totale, 
//  gia' impegnata da RAM Z80/video/audio).
//  Qui costruiamo invece, al mount, solo un INDICE (posizione e
//  lunghezza di ogni settore all'interno del file), e leggiamo i dati
//  veri on-demand con seek()+read() quando l'FDC li richiede.
//
//  Supporta sia DSK standard che EXTENDED (dimensione traccia
//  variabile via tabella, lunghezza settore presa dal campo
//  "actual data length" invece che dal solo codice N — importante
//  per i dischi con protezione che dichiarano una N diversa dalla
//  dimensione fisica reale, esattamente il caso che la versione HLE
//  non poteva gestire).
// ============================================================

#define DSK2_MAX_TRACKS   84   // fino a 42 tracce x 2 lati
#define DSK2_MAX_SECTORS  16

struct Dsk2Sector {
    uint8_t  C = 0, H = 0, R = 0, N = 0;
    uint8_t  st1 = 0, st2 = 0;
    uint32_t fileOffset = 0;
    uint16_t length = 0;
};

struct Dsk2Track {
    bool       present    = false;
    uint8_t    trackNum   = 0;
    uint8_t    sideNum    = 0;
    uint8_t    numSectors = 0;
    Dsk2Sector sectors[DSK2_MAX_SECTORS];
};

class DskImage2 {
public:
    bool mount(const char* path);
    bool isMounted() const { return _mounted; }
    uint8_t numTracks() const { return _numTracks; }
    uint8_t numSides()  const { return _numSides; }

    // Cerca il descrittore di un settore dati (track,side,R). Non legge
    // i dati, solo i metadati (utile per Read ID e per conoscere la
    // lunghezza prima di allocare un buffer).
    const Dsk2Sector* findSector(uint8_t track, uint8_t side, uint8_t R);

    // Cerca una traccia (utile per Read ID: prende il primo settore).
    const Dsk2Track* findTrack(uint8_t track, uint8_t side);

    // Legge i dati di un settore gia' individuato con findSector().
    // Ritorna quanti byte sono stati letti davvero (min(sector.length, maxLen)).
    uint16_t readSectorData(const Dsk2Sector& sec, uint8_t* dest, uint16_t maxLen);

private:
    fs::File  _file;
    bool      _mounted  = false;
    bool      _extended = false;
    uint8_t   _numTracks = 0;
    uint8_t   _numSides  = 1;
    Dsk2Track _tracks[DSK2_MAX_TRACKS];

    bool readTrackInfo(uint8_t slot, uint32_t trackFileOffset, uint32_t trackByteSize);
};

extern DskImage2 currentDsk2;
