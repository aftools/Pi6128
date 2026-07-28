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
#include "config.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>

// ============================================================
//  Storage: SD su bus SPI1 dedicato (come in PiMSX) con fallback su LittleFS.
//
//  Il motivo della scelta e' lo stesso del progetto PiMSX: il
//  display TFT_eSPI usa SPI0, e tenere la SD su SPI1 evita
//  interferenze/contesa del bus tra video e caricamento disco.
//
//  Sia SD.h che LittleFS.h, sul core RP2040 di Earle Philhower,
//  implementano l'interfaccia fs::FS / fs::File: il resto del
//  codice puo' quindi usare fs::File in modo identico
//  indipendentemente dal backend effettivamente attivo.
// ============================================================

enum class StorageBackend { NONE, SD_CARD, FLASH_LITTLEFS };

extern StorageBackend activeStorage;
extern char selectedDiskPath[128];

// Stessa logica di MSXLoader::initSD(): configura esplicitamente i
// pin di SPI1 (RX=MISO, TX=MOSI, SCK, CS) prima di avviare il bus,
// cosi' la SD resta isolata dal bus SPI0 usato dal display.
inline bool storageInitSD() {
    SPI1.setRX(SD_MISO_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setSCK(SD_SCLK_PIN);
    SPI1.setCS(SD_CS_PIN);
    SPI1.begin();
    delay(100);   // stesso margine usato in MSXLoader, la SD ha bisogno di tempo dopo il power-up del bus
    const uint32_t SD_SPI_CLOCK = 10000000; // 10 MHz
    return SD.begin(SD_CS_PIN, SD_SPI_CLOCK, SPI1);
}

inline bool storageInit() {
    Serial.println("[Storage] Inizializzazione SD (SPI1, dedicata, separata dal display su SPI0)...");
    if (storageInitSD()) {
        activeStorage = StorageBackend::SD_CARD;
        Serial.println("[Storage] SD card OK (SPI1)");
        return true;
    }

    Serial.println("[Storage] SD non trovata o non inizializzabile, provo LittleFS...");
    if (LittleFS.begin()) {
        activeStorage = StorageBackend::FLASH_LITTLEFS;
        Serial.println("[Storage] LittleFS OK (fallback su flash interna)");
        return true;
    }

    Serial.println("[Storage] ERRORE: nessuno storage disponibile (ne' SD ne' LittleFS)");
    activeStorage = StorageBackend::NONE;
    return false;
}

inline fs::File storageOpen(const char* path) {
    switch (activeStorage) {
        case StorageBackend::SD_CARD:
            return SD.open(path, FILE_READ);
        case StorageBackend::FLASH_LITTLEFS:
            return LittleFS.open(path, "r");
        default:
            return fs::File();
    }
}

inline bool storageFileExist(const char* path) {
    switch (activeStorage) {
        case StorageBackend::SD_CARD:
            return SD.exists(path);
        case StorageBackend::FLASH_LITTLEFS:
            return LittleFS.exists(path);
        default:
            return false;
    }
    return false;
}


inline void setSelectedDisk(const char* path) {
    if (path) {
        strncpy(selectedDiskPath, path, sizeof(selectedDiskPath) - 1);
        selectedDiskPath[sizeof(selectedDiskPath) - 1] = '\0';
        Serial.printf("Game selected: %s\n", selectedDiskPath);
    }
}


inline const char* storageName() {
    switch (activeStorage) {
        case StorageBackend::SD_CARD:        return "SD (SPI1)";
        case StorageBackend::FLASH_LITTLEFS: return "LittleFS";
        default:                              return "nessuno";
    }
}

// Speculare a MSXLoader::closeFilesystem()
inline void storageEnd() {
    if (activeStorage == StorageBackend::SD_CARD) {
        SD.end();
        SPI1.end();
        Serial.println("[Storage] SD chiusa (SPI1 rilasciata)");
    }
    activeStorage = StorageBackend::NONE;
}