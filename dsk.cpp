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
#include "dsk.h"
#include <string.h>
#include <strings.h>   // strcasecmp

DskImage currentDsk;

// ------------------------------------------------------------------
// Mount
// ------------------------------------------------------------------
bool DskImage::mount(const char* path) {
    _mounted = false;
    _file = storageOpen(path);
    if (!_file) {
        Serial.printf("[DSK] impossibile aprire %s\n", path);
        return false;
    }

    uint8_t header[256];
    if (_file.read(header, 256) != 256) {
        Serial.println("[DSK] errore lettura header disco");
        return false;
    }

    if (memcmp(header, "MV - CPC", 8) == 0) {
        // formato standard, ok
    } else if (memcmp(header, "EXTENDED", 8) == 0) {
        Serial.println("[DSK] Formato EXTENDED DSK non supportato in questa versione");
        return false;
    } else {
        Serial.println("[DSK] Header non riconosciuto: non sembra un file .dsk valido");
        return false;
    }

    _numTracks = header[0x30];
    _numSides  = header[0x31];
    uint16_t stdTrackSize = header[0x32] | (header[0x33] << 8);

    if (_numSides != 1) {
        Serial.println("[DSK] Solo dischi a 1 lato sono supportati in questa versione");
        return false;
    }
    if (_numTracks > DSK_MAX_TRACKS) {
        Serial.printf("[DSK] %d tracce superano il limite interno (%d)\n", _numTracks, DSK_MAX_TRACKS);
        return false;
    }

    uint32_t offset = 256;
    for (uint8_t t = 0; t < _numTracks; t++) {
        if (!readTrackInfo(t, _tracks[t], offset)) {
            Serial.printf("[DSK] errore lettura track-info traccia %d\n", t);
            return false;
        }
        offset += stdTrackSize;
    }

    // Directory: traccia 0, settori logici C1-C4 (convenzione AMSDOS
    // "Data format": 4 settori = 2KB = 64 entry da 32 byte)
    uint16_t got = 0;
    for (uint8_t r = 0xC1; r <= 0xC4; r++) {
        if (!readSectorByR(0, r, _rawDir + got, 512)) {
            Serial.printf("[DSK] errore lettura directory (settore R=%02X) - il disco potrebbe non essere in formato Data standard\n", r);
            return false;
        }
        got += 512;
    }

    _mounted = true;
    Serial.printf("[DSK] montato: %d tracce, %d lato/i, formato Data standard\n", _numTracks, _numSides);
    return true;
}

bool DskImage::readTrackInfo(uint8_t track, DskTrack& out, uint32_t trackFileOffset) {
    uint8_t hdr[256];
    if (!_file.seek(trackFileOffset)) return false;
    if (_file.read(hdr, 256) != 256) return false;

    if (memcmp(hdr, "Track-Info", 10) != 0) {
        Serial.printf("[DSK] Track-Info mancante/corrotto in traccia %d\n", track);
        return false;
    }

    uint8_t sectorSizeCode = hdr[0x14];
    uint8_t numSectors     = hdr[0x15];
    uint16_t sectorSize    = 128 << sectorSizeCode;

    if (numSectors > DSK_MAX_SECTORS_PER_TRACK) numSectors = DSK_MAX_SECTORS_PER_TRACK;
    out.numSectors = numSectors;

    uint32_t dataStart = trackFileOffset + 256;
    for (uint8_t i = 0; i < numSectors; i++) {
        uint8_t* si = hdr + 0x18 + i * 8;
        uint8_t R = si[2];
        out.sectors[i].R          = R;
        out.sectors[i].size       = sectorSize;
        out.sectors[i].fileOffset = dataStart + (uint32_t)i * sectorSize;
    }
    return true;
}

bool DskImage::readSectorByR(uint8_t track, uint8_t R, uint8_t* dest, uint16_t maxLen) {
    if (track >= _numTracks) return false;
    DskTrack& t = _tracks[track];
    for (uint8_t i = 0; i < t.numSectors; i++) {
        if (t.sectors[i].R == R) {
            uint16_t len = min((uint16_t)t.sectors[i].size, maxLen);
            if (!_file.seek(t.sectors[i].fileOffset)) return false;
            return _file.read(dest, len) == len;
        }
    }
    return false;
}

// ------------------------------------------------------------------
// Directory AMSDOS
// ------------------------------------------------------------------
static void trimName(const uint8_t* raw, char* out, int rawLen) {
    int i = 0;
    for (; i < rawLen; i++) {
        char c = raw[i] & 0x7F;   // bit alto = attributi (R/O, system...)
        if (c == ' ') break;
        out[i] = c;
    }
    out[i] = 0;
}

bool DskImage::findFile(const char* name, const char* ext, AmsdosEntry& out) {
    if (!_mounted) return false;

    out = AmsdosEntry();
    strncpy(out.name, name, 8); out.name[8] = 0;
    strncpy(out.ext,  ext,  3); out.ext[3]  = 0;

    struct { uint8_t extent; uint8_t numRecords; uint8_t blockBytes[16]; } extents[32];
    int numExtents = 0;
    uint8_t userFound = 0;

    for (int i = 0; i < DSK_DIR_ENTRIES; i++) {
        const uint8_t* e = _rawDir + i * 32;
        if (e[0] == 0xE5) continue;

        char entryName[9], entryExt[4];
        trimName(e + 1, entryName, 8);
        trimName(e + 9, entryExt, 3);

        if (strcasecmp(entryName, name) != 0 || strcasecmp(entryExt, ext) != 0) continue;
        if (numExtents >= 32) break;

        extents[numExtents].extent     = e[12];
        extents[numExtents].numRecords = e[15];
        memcpy(extents[numExtents].blockBytes, e + 16, 16);
        userFound = e[0];
        numExtents++;
    }

    if (numExtents == 0) return false;

    // Ordina per numero di extent (selection sort: numExtents e' piccolo)
    for (int a = 0; a < numExtents - 1; a++) {
        int m = a;
        for (int b = a + 1; b < numExtents; b++)
            if (extents[b].extent < extents[m].extent) m = b;
        if (m != a) {
            auto tmp = extents[a]; extents[a] = extents[m]; extents[m] = tmp;
        }
    }

    uint32_t totalBytes = 0;
    uint16_t blockIdx   = 0;
    for (int a = 0; a < numExtents; a++) {
        uint32_t extentBytes = (uint32_t)extents[a].numRecords * 128;
        uint16_t blocksThisExtent = (extentBytes + blockSizeBytes() - 1) / blockSizeBytes();
        if (blocksThisExtent > 16) blocksThisExtent = 16;
        for (uint16_t bb = 0; bb < blocksThisExtent && blockIdx < DSK_MAX_BLOCKS_PER_FILE; bb++) {
            uint8_t blk = extents[a].blockBytes[bb];
            if (blk == 0 && bb > 0) break;   // padding di fine file
            out.blocks[blockIdx++] = blk;
        }
        totalBytes += extentBytes;
    }

    out.numBlocks = blockIdx;
    out.sizeBytes = totalBytes;
    out.user      = userFound;
    out.valid     = true;
    return true;
}

uint32_t DskImage::readFile(const AmsdosEntry& entry, uint32_t offset, uint8_t* dest, uint32_t len) {
    if (!entry.valid || !_mounted) return 0;

    const uint32_t sectorsPerTrack = 9;   // formato Data standard
    uint32_t got = 0;

    while (len > 0) {
        uint16_t blockNum = offset / blockSizeBytes();
        uint16_t blockOff = offset % blockSizeBytes();
        if (blockNum >= entry.numBlocks) break;

        uint16_t physBlock = entry.blocks[blockNum];
        // 1 blocco = 2 settori consecutivi; i blocchi 0/1 sono la
        // directory e non compaiono mai nei file (verificato).
        uint32_t globalSectorIndex = (uint32_t)physBlock * 2;
        uint8_t  baseTrack         = globalSectorIndex / sectorsPerTrack;
        uint8_t  baseSectorInTrack = globalSectorIndex % sectorsPerTrack;

        uint16_t toRead = (uint16_t)min((uint32_t)(blockSizeBytes() - blockOff), len);

        uint8_t sectorBuf[512];
        uint16_t doneInBlock = 0;
        while (doneInBlock < toRead) {
            uint32_t sectorDelta   = (blockOff + doneInBlock) / 512;
            uint32_t curSectorAbs  = baseSectorInTrack + sectorDelta;
            uint8_t  curTrack      = baseTrack + curSectorAbs / sectorsPerTrack;
            uint8_t  curSectorIdx  = curSectorAbs % sectorsPerTrack;
            uint8_t  curR          = 0xC1 + curSectorIdx;

            if (!readSectorByR(curTrack, curR, sectorBuf, 512)) return got;

            uint16_t offInSector = (blockOff + doneInBlock) % 512;
            uint16_t chunk = (uint16_t)min((uint32_t)(512 - offInSector), (uint32_t)(toRead - doneInBlock));
            memcpy(dest + got + doneInBlock, sectorBuf + offInSector, chunk);
            doneInBlock += chunk;
        }

        got    += toRead;
        offset += toRead;
        len    -= toRead;
    }
    return got;
}

int DskImage::listFiles(AmsdosEntry* outArray, int maxEntries) {
    int count = 0;
    for (int i = 0; i < DSK_DIR_ENTRIES && count < maxEntries; i++) {
        const uint8_t* e = _rawDir + i * 32;
        if (e[0] == 0xE5) continue;
        if (e[12] != 0) continue;   // solo extent 0, evita duplicati

        AmsdosEntry& out = outArray[count];
        out = AmsdosEntry();
        trimName(e + 1, out.name, 8);
        trimName(e + 9, out.ext, 3);
        out.user  = e[0];
        out.valid = true;
        count++;
    }
    return count;
}
