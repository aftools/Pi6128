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
#include "fdc.h"

FDC fdc;

uint8_t FDC::readMSR() {
    uint8_t msr = 0;

    if (_phase == FdcPhase::IDLE) {
        msr |= 0x80;
    } else if (_phase == FdcPhase::COMMAND) {
        msr |= 0x80 | 0x10;
    } else if (_phase == FdcPhase::EXECUTION) {
        if (_execIndex < _execLength) {
            msr |= 0x80 | 0x40 | 0x20 | 0x10;
        } else {
            enterResultPhase();
            msr |= 0x80 | 0x40 | 0x10;
        }
    } else if (_phase == FdcPhase::RESULT) {
        if (_resIndex < _resCount) {
            msr |= 0x80 | 0x40 | 0x10;
        } else {
            _phase = FdcPhase::IDLE;
            msr |= 0x80;
        }
    }

    return msr;
}

uint8_t FDC::readData() {
    if (_phase == FdcPhase::EXECUTION) {
        if (_execIndex < _execLength) {
            return _execBuffer[_execIndex++];
        }
    } else if (_phase == FdcPhase::RESULT) {
        if (_resIndex < _resCount) {
            return _resBuffer[_resIndex++];
        }
    }
    return 0xFF;
}

void FDC::writeData(uint8_t val) {
    if (_phase == FdcPhase::IDLE) {
        _phase = FdcPhase::COMMAND;
        _cmdCount = 0;
        _cmdBuffer[_cmdCount++] = val;
        determineCmdLength(val);
    } else if (_phase == FdcPhase::COMMAND) {
        if (_cmdCount < FDC_CMD_BUF_MAX) _cmdBuffer[_cmdCount++] = val;
    }

    if (_phase == FdcPhase::COMMAND && _cmdCount == _cmdLength) {
        processCommand();
    }
}

void FDC::determineCmdLength(uint8_t cmd) {
    uint8_t c = cmd & 0x1F;
    if (c == 0x03) _cmdLength = 3;        // Specify
    else if (c == 0x04) _cmdLength = 2;   // Sense Drive Status
    else if (c == 0x07) _cmdLength = 2;   // Recalibrate
    else if (c == 0x08) _cmdLength = 1;   // Sense Int Status
    else if (c == 0x0F) _cmdLength = 3;   // Seek
    else if (c == 0x06 || c == 0x0C) _cmdLength = 9;   // Read Data / Read Deleted Data
    else if (c == 0x0A) _cmdLength = 2;   // Read ID
    else _cmdLength = 1;                  // Sconosciuto/non valido
}

void FDC::processCommand() {
    uint8_t cmd = _cmdBuffer[0] & 0x1F;
    _phase = FdcPhase::EXECUTION;
    _execLength = 0;
    _execIndex  = 0;

    if (cmd == 0x03) {                      // Specify
        enterResultPhase();
    } else if (cmd == 0x04) {               // Sense Drive Status
        uint8_t st3 = 0x20;
        if (_regC == 0) st3 |= 0x10;
        resClearAndSet({st3});
        _phase = FdcPhase::RESULT;
    } else if (cmd == 0x07) {               // Recalibrate
        _regC = 0;
        enterResultPhase();
    } else if (cmd == 0x08) {               // Sense Interrupt Status
        resClearAndSet({(uint8_t)0x20, _regC});
        _phase = FdcPhase::RESULT;
    } else if (cmd == 0x0F) {               // Seek
        _regC = _cmdBuffer[2];
        enterResultPhase();
    } else if (cmd == 0x06) {               // Read Data
        uint8_t hd = (_cmdBuffer[1] >> 2) & 1;
        _regC = _cmdBuffer[2];
        _regH = _cmdBuffer[3];
        _regR = _cmdBuffer[4];
        _regN = _cmdBuffer[5];

        if (_dsk) {
            const Dsk2Sector* sec = _dsk->findSector(_regC, hd, _regR);
            if (sec) {
                uint16_t len = min((uint16_t)sec->length, (uint16_t)FDC_EXEC_BUF_MAX);
                _execLength = _dsk->readSectorData(*sec, _execBuffer, len);
                _st1 = 0;
                _st2 = 0;
            } else {
                _st1 = 0x04;   // No data
                enterResultPhase();
            }
        } else {
            _st1 = 0x04;
            enterResultPhase();
        }
    } else if (cmd == 0x0A) {               // Read ID
        if (_dsk) {
            uint8_t hd = (_cmdBuffer[1] >> 2) & 1;
            const Dsk2Track* trk = _dsk->findTrack(_regC, hd);
            if (trk && trk->numSectors > 0) {
                const Dsk2Sector& sec = trk->sectors[0];
                _regC = sec.C; _regH = sec.H; _regR = sec.R; _regN = sec.N;
                _st1 = 0; _st2 = 0;
            } else {
                _st1 = 0x04;
            }
        } else {
            _st1 = 0x04;
        }
        enterResultPhase();
    } else {                                // Comando non valido
        _st0 = 0x80;
        resClearAndSet({_st0});
        _phase = FdcPhase::RESULT;
    }
}

void FDC::enterResultPhase() {
    _phase = FdcPhase::RESULT;
    uint8_t cmd = _cmdBuffer[0] & 0x1F;

    if (cmd == 0x06 || cmd == 0x0A || cmd == 0x05) {   // Read Data / Read ID / Write Data
        _st0 = 0x40;
        if (_st1 != 0) _st0 |= 0x40;
        else _st0 = 0x00;
        resClearAndSet({_st0, _st1, _st2, _regC, _regH, _regR, _regN});
    } else if (cmd == 0x03 || cmd == 0x07 || cmd == 0x0F) {   // Specify / Recalibrate / Seek
        _resCount = 0;
        _resIndex = 0;
        _phase = FdcPhase::IDLE;
    }
}
