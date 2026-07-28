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
#include "dsk_sectors.h"
#include <string.h>

DskImage2 currentDsk2;

bool DskImage2::mount(const char* path) {
    _mounted = false;
    for (auto& t : _tracks) t = Dsk2Track();

    _file = storageOpen(path);
    if (!_file) {
        Serial.printf("[DSK2] impossibile aprire %s\n", path);
        return false;
    }

    uint8_t header[256];
    if (_file.read(header, 256) != 256) {
        Serial.println("[DSK2] errore lettura header disco");
        return false;
    }

    // controlla se i primi byte sono "EXTENDED"
    _extended = (memcmp(header, "EXTENDED", 8) == 0);
    bool standardOk = (memcmp(header, "MV - CPC", 8) == 0);
    if (!_extended && !standardOk) {
        Serial.println("[DSK2] Header non riconosciuto: non e' un file .dsk valido");
        return false;
    }

    _numTracks = header[0x30];
    _numSides  = header[0x31];
    uint16_t standardTrackSize = header[0x32] | (header[0x33] << 8);

    uint16_t totalSlots = (uint16_t)_numTracks * (uint16_t)_numSides;
    if (totalSlots > DSK2_MAX_TRACKS) {
        Serial.printf("[DSK2] %d tracce*lati superano il limite interno (%d)\n", totalSlots, DSK2_MAX_TRACKS);
        return false;
    }

    // Per capire la dimensione massima del file uso la size del file stesso.
    uint32_t fileSize = _file.size();

    uint32_t offset = 256;   // dopo l'header principale
    uint8_t  slot = 0;

    for (uint16_t t = 0; t < totalSlots; t++) {
        if (offset >= fileSize) break;

        uint32_t trackSize = _extended
            ? ((uint32_t)header[0x34 + t] * 256)
            : standardTrackSize;

        if (trackSize == 0) continue;   // traccia non formattata: non occupa spazio nel file

        if (!readTrackInfo(slot, offset, trackSize)) {
            Serial.printf("[DSK2] errore lettura track-info allo slot %d (offset file %lu)\n",
                          slot, (unsigned long)offset);
            return false;
        }
        slot++;
        offset += trackSize;
    }

    _mounted = true;
    Serial.printf("[DSK2] montato: %d tracce, %d lato/i, %s, %d tracce/lati effettivi letti\n",
                  _numTracks, _numSides, _extended ? "EXTENDED" : "standard", slot);
    return true;
}

bool DskImage2::readTrackInfo(uint8_t slot, uint32_t trackFileOffset, uint32_t trackByteSize) {
    uint8_t hdr[256];
    if (!_file.seek(trackFileOffset)) return false;
    if (_file.read(hdr, 256) != 256) return false;

    Dsk2Track& trk = _tracks[slot];
    trk.trackNum   = hdr[0x10];
    trk.sideNum    = hdr[0x11];
    trk.numSectors = hdr[0x15];
    if (trk.numSectors > DSK2_MAX_SECTORS) trk.numSectors = DSK2_MAX_SECTORS;
    trk.present    = true;

    uint32_t sectorDataOffset = trackFileOffset + 256;

    for (uint8_t s = 0; s < trk.numSectors; s++) {
        uint8_t* si = hdr + 0x18 + s * 8;
        uint8_t  C = si[0], H = si[1], R = si[2], N = si[3];
        uint8_t  st1 = si[4], st2 = si[5];
        uint16_t actualLen = si[6] | (si[7] << 8);

        // Su Extended DSK la lunghezza REALE viene dal
        // campo "actual data length" del sector-info (puo' differire da
        // 128<<N nei dischi con protezione); su DSK standard si usa
        // sempre 128<<N.
        uint16_t dataLen = _extended ? actualLen : (uint16_t)(128 << N);

        Dsk2Sector& sec = trk.sectors[s];
        sec.C = C; sec.H = H; sec.R = R; sec.N = N;
        sec.st1 = st1; sec.st2 = st2;
        sec.fileOffset = sectorDataOffset;
        sec.length     = dataLen;

        sectorDataOffset += dataLen;
    }

    return true;
}

const Dsk2Track* DskImage2::findTrack(uint8_t track, uint8_t side) {
    for (uint8_t i = 0; i < DSK2_MAX_TRACKS; i++) {
        if (_tracks[i].present && _tracks[i].trackNum == track && _tracks[i].sideNum == side) {
            return &_tracks[i];
        }
    }
    return nullptr;
}

const Dsk2Sector* DskImage2::findSector(uint8_t track, uint8_t side, uint8_t R) {
    const Dsk2Track* trk = findTrack(track, side);
    if (!trk) return nullptr;
    for (uint8_t i = 0; i < trk->numSectors; i++) {
        if (trk->sectors[i].R == R) return &trk->sectors[i];
    }
    return nullptr;
}

uint16_t DskImage2::readSectorData(const Dsk2Sector& sec, uint8_t* dest, uint16_t maxLen) {
    uint16_t toRead = min(sec.length, maxLen);
    if (!_file.seek(sec.fileOffset)) return 0;
    return (uint16_t)_file.read(dest, toRead);
}
