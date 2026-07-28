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
#include "pico/time.h"

// ============================================================
//  Aggancio al tempo reale (throttling)
//
//  Lo Z80 del CPC gira a 4MHz: 1 T-state = 0.25us, una scanline
//  CRTC (256 T-state, vedi crtcTickScanline()) dura quindi 64us
//  reali. Senza throttling il loop() gira alla velocita' massima
//  permessa dal Pico: overcloccando (o anche solo cambiando quanto
//  e' "leggero" lo step Z80) la CPU virtuale finisce per correre
//  piu' veloce del CPC reale, rompendo qualunque routine a tempo.
//
//  Uso:
//    - timingInit() UNA VOLTA, subito dopo firmwareReady = true
//      (il warm-up va lasciato a piena velocita', altrimenti il
//      boot del firmware richiederebbe secondi reali)
//    - timingThrottleScanline() ad ogni scanline consumata, cioe'
//      dentro il while(tstateAccum >= 256) del loop(), subito dopo
//      crtcTickScanline()
// ============================================================

#define US_PER_SCANLINE   64      // 256 T-state / 4 MHz
#define MAX_CATCHUP_US    20000   // non rincorrere oltre ~1 frame di ritardo

static uint64_t nextScanlineDeadlineUs = 0;

inline void timingInit() {
    nextScanlineDeadlineUs = time_us_64();
}

inline void timingThrottleScanline() {
    nextScanlineDeadlineUs += US_PER_SCANLINE;

    uint64_t now = time_us_64();

    // Troppo indietro (stallo lungo: Serial bloccato, debugger, ecc.):
    // risincronizza invece di "rincorrere" a piena velocita', che
    // sarebbe percepito come un balzo in avanti innaturale.
    if (now > nextScanlineDeadlineUs + MAX_CATCHUP_US) {
        nextScanlineDeadlineUs = now;
        return;
    }

    while (time_us_64() < nextScanlineDeadlineUs) {
        tight_loop_contents();
    }
}
