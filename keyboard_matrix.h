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
 */
#pragma once
#include <Arduino.h>

// ============================================================
//  Matrice tastiera CPC: 10 righe x 8 bit (attivo basso = tasto
//  premuto). Definita UNA sola volta in cpc464_pico.ino (con storage
//  reale) e dichiarata qui come extern, perche' viene condivisa tra
//  file inclusi nello sketch (keyboard_ps2.h, joystick.h) E un file
//  .cpp compilato separatamente (AY3891.cpp): senza "extern" ognuno
//  si ritroverebbe una copia propria invece di condividere lo stato.
// ============================================================
extern volatile uint8_t keyMatrix[10];
extern volatile uint8_t cpcKeyboardRowSel; 
extern volatile uint8_t kbdRow6Mask;   // contributo SOLO tastiera sulla riga 6
extern volatile uint8_t joyRow6Mask;   // contributo SOLO joystick sulla riga 6
