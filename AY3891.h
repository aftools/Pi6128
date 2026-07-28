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
 *
 * AY3891.h  –  Emulatore AY-3-8910 (PSG) per MSX su RP2040
 *
 * MODIFICHE per uso su Amstrad CPC464 (rispetto all'originale PiMSX):
 *   - AY_CLOCK_HZ: 1000000 (clock reale PSG del CPC) invece di
 *     1789772 (clock PSG MSX NTSC)
 *   - Reg. 14: sul CPC il PSG non pilota un joystick come su MSX, ma
 *     e' il canale con cui il PPI 8255 legge la matrice tastiera
 *     (10x8): la SELEZIONE riga avviene via Port C del PPI
 *     (cpcKeyboardRowSel, scritta in ppi8255.h), la LETTURA dati
 *     avviene qui in readData() quando reg. selezionato = 14, che
 *     restituisce keyMatrix[cpcKeyboardRowSel] (vedi keyboard_matrix.h
 *     e i punti marcati "MODIFICA CPC" in AY3891.cpp). Il registro 14
 *     resta comunque scrivibile in writeData() per compatibilita' con
 *     l'interfaccia AY3891 generica, ma la scrittura non ha alcun
 *     effetto sulla selezione riga.
 *   - Tutto il resto (sintesi toni/rumore/inviluppo, mixer, ring
 *     buffer, IRQ PWM) e' invariato rispetto all'originale.
 * ============================================================
 * Output : GPIO15 → PWM 8-bit → filtro RC → PAM8403
 *
 * Architettura:
 *   • Core0 chiama update() nel loop di throttle per riempire il ring buffer
 *   • ISR PWM (Core0) legge un campione per ciclo PWM → duty cycle
 *
 * Scelte progettuali:
 *   • Sample rate  22050 Hz
 *   • Risoluzione PWM  8-bit (256 livelli)
 *   • Accumulatori Q10 interi – zero virgola mobile
 *   • LUT logaritmica hardware-accurate (16 livelli, misurati sul chip reale)
 *   • Ring buffer 1024 campioni (~46 ms): margine ampio contro underrun da throttle
 *
 * Strategia anti-glitch (tick periodici):
 *   Il problema classico è che delayMicroseconds() blocca Core0 mentre l'ISR
 *   consuma il buffer. Con un frame budget di ~8 ms e un delay di altri ~8 ms,
 *   il buffer da 512 campioni (23 ms) si svuota prima del prossimo update().
 *
 *   Soluzione: update() diventa DEMAND-DRIVEN.
 *   Invece di produrre sempre un numero fisso di campioni, produce abbastanza
 *   campioni per portare il buffer a AY_BUF_HIGH_WATER.
 *   Il loop di throttle in MSXloop chiama psg.update() invece di
 *   delayMicroseconds(), cedendo CPU senza mai lasciare il buffer vuoto.
 *
 * Costanti Q10 precompilate (×1024 del valore reale):
 *   TONE_STEP  = AY_CLK × 128  / SAMPLE_RATE  = 10 389
 *   NOISE_STEP = AY_CLK × 64   / SAMPLE_RATE  =  5 194
 *   ENV_STEP   = AY_CLK × 4    / SAMPLE_RATE  =    324
 *
 * Mappa registri AY-3-8910:
 *   R0-R5   Periodo tono A/B/C (12-bit, diviso in low+high)
 *   R6      Periodo rumore (5-bit)
 *   R7      Mixer: bit 0-2 = tono A/B/C disable, bit 3-5 = rumore A/B/C disable
 *   R8-R10  Volume A/B/C (4-bit) | bit4=envelope mode
 *   R11-R12 Periodo envelope (16-bit)
 *   R13     Shape envelope (4-bit): CONT ATT ALT HOLD
 *   R14     Porta A I/O (joystick 1, input) -- su CPC: vedi nota sopra, riuso per tastiera
 *   R15     Porta B I/O (joystick 2, input)
 *
 */

#pragma once
#include <Arduino.h>
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

// ─── Configurazione hardware ──────────────────────────────────────────────────
#define AY_AUDIO_PIN      15          // GPIO15 = slice 7, canale B
#define AY_SAMPLE_RATE    22050       // Hz
#define AY_CLOCK_HZ       1000000UL  // MODIFICA CPC: clock PSG reale del CPC

// ─── Volume master ────────────────────────────────────────────────────────────
// Range 0-255: 255 = massimo hardware, 0 = silenzio assoluto.
// Incorporato in _scaledVolLUT una volta in begin(); zero overhead a runtime.
#define AY_MASTER_VOLUME  20         // ← regola qui il volume (0-255)

// ─── Ring buffer (potenza di 2) ───────────────────────────────────────────────
// 1024 campioni = ~46 ms: assorbe il delay di throttle (~8 ms) +
// variazioni di timing senza underrun.
#define AY_BUF_BITS       10
#define AY_BUF_SIZE       (1 << AY_BUF_BITS)   // 1024 campioni (~46 ms)
#define AY_BUF_MASK       (AY_BUF_SIZE - 1)

// ─── Watermark buffer (update demand-driven) ─────────────────────────────────
// update() produce campioni fino a raggiungere HIGH_WATER, poi si ferma.
// Chiamata nel loop di throttle (vedi MSXloop) invece di delayMicroseconds():
// mantiene il buffer pieno senza bloccare l'ISR.
//   HIGH_WATER 800 = ~36 ms  (< BUF_SIZE − 32: lascia margine di sicurezza)
//   LOW_WATER  368 = ~1 frame (soglia diagnostica: se scende qui c'è un problema)
#define AY_BUF_HIGH_WATER 950
#define AY_BUF_LOW_WATER  368

// ─── Costanti Q10 (integer × 1024) ───────────────────────────────────────────
#define AY_TONE_STEP_Q10   ((uint32_t)(AY_CLOCK_HZ * 128UL / AY_SAMPLE_RATE))
#define AY_NOISE_STEP_Q10  ((uint32_t)(AY_CLOCK_HZ * 64UL  / AY_SAMPLE_RATE))
#define AY_ENV_STEP_Q10    ((uint32_t)(AY_CLOCK_HZ * 4UL   / AY_SAMPLE_RATE))

// ─────────────────────────────────────────────────────────────────────────────
class AY3891 {
public:
    AY3891();

    // Init / reset
    void    begin();
    void    reset();

    // Interfaccia Z80 (porte 0xA0 / 0xA1 / 0xA2)
    void    selectRegister(uint8_t reg);   // OUT 0xA0
    void    writeData(uint8_t value);       // OUT 0xA1
    uint8_t readData() const;               // IN  0xA2

    uint8_t getSelectedRegister() const { return _selReg; }

    // Stato joystick porta A e B
    void    setPortA(uint8_t state)  { _portA = state; }
    uint8_t getPortA()         const { return _portA;  }
    void    setPortB(uint8_t state)  { _portB = state; }
    uint8_t getPortB()         const { return _portB;  }

    // Campioni disponibili nel buffer (lettura ISR-safe via snapshot atomico)
    uint16_t available() const {
        return (uint16_t)((_wIdx - _rIdx) & AY_BUF_MASK);
    }

    // Produce campioni finché il buffer non raggiunge AY_BUF_HIGH_WATER.
    // Chiamare sia una volta per frame (dopo pushFrameToDisplay) sia nel
    // loop di throttle, in sostituzione di delayMicroseconds().
    // Se il buffer è già sopra HIGH_WATER la funzione ritorna subito (no-op).
    void    update();

private:
    uint8_t _regs[16];
    uint8_t _selReg;
    uint8_t _portA;
    uint8_t _portB;

    // ── Canali tono ──────────────────────────────────────────────────────────
    struct ToneCh {
        uint32_t accum;
        uint8_t  out;
    };
    ToneCh _chA, _chB, _chC;

    // ── Rumore ───────────────────────────────────────────────────────────────
    uint32_t _noiseAccum;
    uint32_t _noiseLFSR;
    uint8_t  _noiseOut;

    // ── Envelope (modello counter + attack XOR, come su openMSX/MAME) ─────
    uint32_t _envAccum;
    uint8_t  _envCounter;
    uint8_t  _envAttack;
    bool     _envHolding;
    uint8_t  _envVol;
    uint8_t  _envLevel;

    // ── Livelli canale precalcolati ───────────────────────────────────────────
    uint8_t  _levelA;
    uint8_t  _levelB;
    uint8_t  _levelC;

    // ── Ring buffer ──────────────────────────────────────────────────────────
    volatile uint8_t  _buf[AY_BUF_SIZE];
    volatile uint16_t _wIdx;
    volatile uint16_t _rIdx;

    uint _pwmSlice;

    static const uint8_t _volLUT[16];
    uint8_t _scaledVolLUT[16];

    static AY3891* _instance;
    static void    _pwmISR();

    void    _stepTone(ToneCh& ch, uint16_t period);
    void    _stepNoise(uint8_t noisePeriod);
    void    _stepEnvelope();
    void    _initEnvelope();
    uint8_t _mixSample();
    void    _pushSample(uint8_t s);
};
