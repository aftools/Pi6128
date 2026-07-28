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

// ============================================================
//  Istrumentazione minimale per confrontare CPU / audio / video
//  rispetto al budget di tempo reale (PAL = 20000us/frame a 50Hz).
//
//  Nota: cpuBusyUs/audioBusyUs sono scritti solo da core0,
//  videoBusyUs/videoFrames/videoSkipped solo da core1. La lettura
//  per il report avviene da core0: su RP2040 una singola read/write
//  a 32 bit e' atomica, quindi non serve un mutex per statistiche
//  puramente indicative come queste.
// ============================================================

struct PerfStats {
    volatile uint32_t cpuBusyUs    = 0;   // tempo in z80.step() [core0]
    volatile uint32_t audioBusyUs  = 0;   // tempo in ay.update() [core0]
    volatile uint32_t videoBusyUs  = 0;   // tempo in renderFrame() [core1]
    volatile uint32_t videoFrames  = 0;   // frame realmente disegnati
    volatile uint32_t videoSkipped = 0;   // frame saltati per VIDEO_FRAME_SKIP
};

extern PerfStats perf;

#ifndef PERF_REPORT_INTERVAL_MS
#define PERF_REPORT_INTERVAL_MS 1000
#endif

// Da chiamare una volta per iterazione di loop() (core0).
inline void perfMaybeReport() {
    static uint32_t lastReport = 0;
    uint32_t now = millis();
    if (now - lastReport < PERF_REPORT_INTERVAL_MS) return;
    uint32_t elapsedMs = now - lastReport;
    lastReport = now;

    uint32_t cpuUs   = perf.cpuBusyUs;    perf.cpuBusyUs    = 0;
    uint32_t audioUs = perf.audioBusyUs;  perf.audioBusyUs  = 0;
    uint32_t vidUs   = perf.videoBusyUs;  perf.videoBusyUs  = 0;
    uint32_t frames  = perf.videoFrames;  perf.videoFrames  = 0;
    uint32_t skipped = perf.videoSkipped; perf.videoSkipped = 0;

    uint32_t budgetUs = elapsedMs * 1000UL;
    if (budgetUs == 0) return;

    Serial.printf(
        "[PERF] cpu=%2lu%%  audio=%2lu%%  video=%2lu%%  (%lu fps, %lu skip)  window=%lums\n",
        (unsigned long)(100UL * cpuUs   / budgetUs),
        (unsigned long)(100UL * audioUs / budgetUs),
        (unsigned long)(100UL * vidUs   / budgetUs),
        (unsigned long)frames,
        (unsigned long)skipped,
        (unsigned long)elapsedMs);
}

// Macro comode per misurare un blocco senza riscrivere micros() ogni volta.
#define PERF_TIME(counter, code) do \
    { \
        uint32_t _t0 = micros(); \
        code; \
        counter += micros() - _t0; \
    } while (0)
    