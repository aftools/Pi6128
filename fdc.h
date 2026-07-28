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
#include <initializer_list>
#include "dsk_sectors.h"

// ============================================================
//  FDC 
//  uPD765: Specify, Sense Drive Status, Recalibrate, Sense Interrupt
//  Status, Seek, Read Data, Read ID. Implementata con allocazione dinamica 
//
//  Porte CPC (verificate su CPCWiki "Default I/O Port Summary"):
//    &FB7E  Main Status Register   (lettura)
//    &FB7F  Data Register          (lettura/scrittura)
//    &FA7E  Controllo motore       (scrittura, ignorato: non serve
//           motion reale per l'emulazione)
// ============================================================

enum class FdcPhase { IDLE, COMMAND, EXECUTION, RESULT };

#define FDC_CMD_BUF_MAX  9
#define FDC_RES_BUF_MAX  7
#define FDC_EXEC_BUF_MAX 1024   // margine ampio: i settori CPC reali non superano 512-1024 byte

class FDC {
public:
    void insertDisk(DskImage2* dsk) { _dsk = dsk; }

    uint8_t readMSR();
    uint8_t readData();
    void    writeData(uint8_t val);

private:
    DskImage2* _dsk = nullptr;
    FdcPhase   _phase = FdcPhase::IDLE;

    uint8_t _cmdBuffer[FDC_CMD_BUF_MAX];
    uint8_t _cmdLength = 0;
    uint8_t _cmdCount  = 0;

    uint8_t _resBuffer[FDC_RES_BUF_MAX];
    uint8_t _resCount  = 0;   // quanti byte validi restano in resBuffer
    uint8_t _resIndex  = 0;   // prossimo byte da restituire (equivalente a shift())

    uint8_t  _execBuffer[FDC_EXEC_BUF_MAX];
    uint16_t _execLength = 0;
    uint16_t _execIndex  = 0;

    uint8_t _st0 = 0, _st1 = 0, _st2 = 0;
    uint8_t _regC = 0, _regH = 0, _regR = 0, _regN = 0;

    void determineCmdLength(uint8_t cmd);
    void processCommand();
    void enterResultPhase();

    void resPush(uint8_t v) { if (_resCount < FDC_RES_BUF_MAX) _resBuffer[_resCount++] = v; }
    void resClearAndSet(std::initializer_list<uint8_t> vals) {
        _resCount = 0; _resIndex = 0;
        for (uint8_t v : vals) resPush(v);
    }
};

extern FDC fdc;
