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
#include "config.h"
#include "keyboard_matrix.h"

// Joystick 0: l'UNICO joystick fisico reale della CPC (porta DB9).
// Vive sulla Riga 9 della matrice, bit0-5, senza condivisione con
// lettere della tastiera (bit7 di questa riga e' Del, gestito a parte
// in keyboard.h - non interferisce col joystick).
// La Riga 6 (F/G/T/R/5/6) e' invece "Joystick 1", il secondo stick
// opzionale storico, che quasi nessun gioco legge: NON va usata per
// il joystick principale.
#define JOY_ROW 9
enum { JOY_BIT_UP=0, JOY_BIT_DOWN=1, JOY_BIT_LEFT=2, JOY_BIT_RIGHT=3,
       JOY_BIT_FIRE2=4, JOY_BIT_FIRE1=5 };

// Bit mask per la lettura "grezza" via readHardware(), indipendente
// dal layout della matrice CPC (usata ad es. nei menu prima dell'avvio
// dell'emulatore). Logica negativa: bit=0 significa "premuto".
enum {
    JOY_HW_UP    = 0x01,
    JOY_HW_DOWN  = 0x02,
    JOY_HW_LEFT  = 0x04,
    JOY_HW_RIGHT = 0x08,
    JOY_HW_FIRE  = 0x10,
};

inline void joystickInit() {
    pinMode(JOY_UP_PIN,    INPUT_PULLUP);
    pinMode(JOY_DOWN_PIN,  INPUT_PULLUP);
    pinMode(JOY_LEFT_PIN,  INPUT_PULLUP);
    pinMode(JOY_RIGHT_PIN, INPUT_PULLUP);
    pinMode(JOY_FIRE1_PIN, INPUT_PULLUP);
    pinMode(JOY_FIRE2_PIN, INPUT_PULLUP);
}

// Lettura diretta dell'hardware, utilizzabile anche fuori dal ciclo
// dell'emulatore (es. per navigare un menu di selezione .dsk).
// Richiede solo che joystickInit() sia gia' stato chiamato.
// Ritorna un byte con bit a 0 = direzione/fire premuti, default 0xFF.
// Il fire riportato e' il pulsante 1 (JOY_FIRE1_PIN); se ti serve
// anche il fire2, leggilo separatamente (vedi joystickReadFire2()).
inline uint8_t joystickReadHardware() {
    uint8_t state = 0xFF;

    if (digitalRead(JOY_UP_PIN)    == LOW) state &= ~JOY_HW_UP;
    if (digitalRead(JOY_DOWN_PIN)  == LOW) state &= ~JOY_HW_DOWN;
    if (digitalRead(JOY_LEFT_PIN)  == LOW) state &= ~JOY_HW_LEFT;
    if (digitalRead(JOY_RIGHT_PIN) == LOW) state &= ~JOY_HW_RIGHT;
    if (digitalRead(JOY_FIRE1_PIN) == LOW) state &= ~JOY_HW_FIRE;

    return state;
}

// Lettura separata del secondo pulsante fire
inline bool joystickReadFire2() {
    return digitalRead(JOY_FIRE2_PIN) == LOW;
}

inline void joySetBit(uint8_t bit, bool active) {
    if (active) joyRow6Mask &= ~(1 << bit);
    else        joyRow6Mask |=  (1 << bit);
}

inline void joystickTask() {
    joySetBit(JOY_BIT_UP,    digitalRead(JOY_UP_PIN)    == LOW);
    joySetBit(JOY_BIT_DOWN,  digitalRead(JOY_DOWN_PIN)  == LOW);
    joySetBit(JOY_BIT_LEFT,  digitalRead(JOY_LEFT_PIN)  == LOW);
    joySetBit(JOY_BIT_RIGHT, digitalRead(JOY_RIGHT_PIN) == LOW);
    joySetBit(JOY_BIT_FIRE1, digitalRead(JOY_FIRE1_PIN) == LOW);
    joySetBit(JOY_BIT_FIRE2, digitalRead(JOY_FIRE2_PIN) == LOW);
    keyMatrix[JOY_ROW] = kbdRow6Mask & joyRow6Mask;
}