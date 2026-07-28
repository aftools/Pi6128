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
#include "PS2Keyboard.h" // classe reale di PiMSX
#include "keyboard_matrix.h"
#include "joystick.h"

// ============================================================
//  Tastiera PS/2: la lettura fisica (bit-banging via IRQ sul CLK,
//  buffer circolare scancode) e' delegata alla classe PS2Keyboard
//  di PiMSX (vedi PS2Keyboard.h/.cpp). Qui restano solo:
//    - la traduzione scancode SET2 -> posizione matrice CPC 10x8
//    - il consumo del buffer nel task chiamato dal loop() di core0
//
//  NOTA: la mappatura riga/colonna della matrice CPC464 riportata qui
//  e' stata verificata rispetto alla matrice hardware ufficiale
//  (fonte: cpctech.cpcwiki.de/docs/keyboard.html).
//
//  NOTA SU BLOCNUM: questo driver PS/2 e' solo in ricezione (nessun
//  comando host->tastiera), quindi non posso forzare il LED NumLock
//  ne' garantire che il tastierino numerico mandi i codici "numerici"
//  invece di quelli estesi cursore/Home/End/PgUp/PgDn. Per questo i
//  tasti funzione CPC (f0..f9) sono mappati SOLO su F1..F10 della
//  tastiera PC, non sul tastierino numerico.
// ============================================================

extern PS2Keyboard ps2Kbd;   // istanziata in Pi464.ino

// ---- Selezione layout tastiera fisica PS/2 -------------------
// Metti questa define in config.h (o qui sopra), 1 = IT, 0 = EN/US
#ifndef CPC_KBD_LAYOUT_IT
#define CPC_KBD_LAYOUT_IT 1
#endif

struct CpcKeyPos { int8_t row; int8_t bit; };

// Stato Shift tracciato lato PS/2, usato solo per risolvere il caso
// ambiguo "-"/"_" (vedi sotto). Aggiornato in keyboardTask().
static bool ps2ShiftDown = false;

// Funzione di traduzione: sostituisce la tabella static con inizializzatori designati
static CpcKeyPos translateScancode(uint8_t code, bool extended, bool shiftHeld) {
    if (extended) {
        // Codici estesi (preceduti da 0xE0)
        switch (code) {
            case 0x75: return {0,0}; // Cursor Up
            case 0x72: return {0,2}; // Cursor Down
            case 0x6B: return {1,0}; // Cursor Left
            case 0x74: return {0,1}; // Cursor Right
            case 0x14: return {2,7}; // R-CTRL -> stesso bit di L-CTRL
            case 0x70: return {1,1}; // INSERT -> Copy
            case 0x6C: return {2,0}; // HOME   -> Clr
            case 0x71: return {9,7}; // CANC (Delete) -> Del, come Backspace
            default:   return {-1,-1};
        }
    } else {
        switch (code) {
            // Lettere
            case 0x1C: return {8,5}; // A
            case 0x32: return {6,6}; // B
            case 0x21: return {7,6}; // C
            case 0x23: return {7,5}; // D
            case 0x24: return {7,2}; // E
            case 0x2B: return {6,5}; // F
            case 0x34: return {6,4}; // G
            case 0x33: return {5,4}; // H
            case 0x43: return {4,3}; // I
            case 0x3B: return {5,5}; // J
            case 0x42: return {4,5}; // K
            case 0x4B: return {4,4}; // L
            case 0x3A: return {4,6}; // M
            case 0x31: return {5,6}; // N
            case 0x44: return {4,2}; // O
            case 0x4D: return {3,3}; // P
            case 0x15: return {8,3}; // Q
            case 0x2D: return {6,2}; // R
            case 0x1B: return {7,4}; // S
            case 0x2C: return {6,3}; // T
            case 0x3C: return {5,2}; // U
            case 0x2A: return {6,7}; // V
            case 0x1D: return {7,3}; // W
            case 0x22: return {7,7}; // X
            case 0x35: return {5,3}; // Y
            case 0x1A: return {8,7}; // Z
            // Cifre (riga superiore)
            case 0x45: return {4,0}; // 0
            case 0x16: return {8,0}; // 1
            case 0x1E: return {8,1}; // 2
            case 0x26: return {7,1}; // 3
            case 0x25: return {7,0}; // 4
            case 0x2E: return {6,1}; // 5
            case 0x36: return {6,0}; // 6
            case 0x3D: return {5,1}; // 7
            case 0x3E: return {5,0}; // 8
            case 0x46: return {4,1}; // 9
            // Simboli / funzioni
            case 0x29: return {5,7}; // SPACE
            case 0x5A: return {2,2}; // ENTER -> Return (tasto grande)
            case 0x66: return {9,7}; // BACKSPACE -> Del
            case 0x76: return {8,2}; // ESC
            case 0x12: return {2,5}; // L-SHIFT
            case 0x59: return {2,5}; // R-SHIFT -> stesso bit di L-SHIFT
            case 0x14: return {2,7}; // L-CTRL
            case 0x0D: return {8,4}; // TAB
            case 0x58: return {8,6}; // CAPS LOCK

            // ---- Tasti funzione CPC (keypad: f0..f9) ----
            // Mappati SOLO su F1..F10 PC (vedi nota NumLock in testa al file)
            case 0x05: return {1,5}; // F1  -> f1
            case 0x06: return {1,6}; // F2  -> f2
            case 0x04: return {0,5}; // F3  -> f3
            case 0x0C: return {2,4}; // F4  -> f4
            case 0x03: return {1,4}; // F5  -> f5
            case 0x0B: return {0,4}; // F6  -> f6
            case 0x83: return {1,2}; // F7  -> f7
            case 0x0A: return {1,3}; // F8  -> f8
            case 0x01: return {0,3}; // F9  -> f9
            case 0x09: return {1,7}; // F10 -> f0

#if CPC_KBD_LAYOUT_IT
            // =========== LAYOUT TASTIERA FISICA ITALIANA ===========
            case 0x41: return {3,7};   // , (virgola)          -> , / >
            case 0x49: return {4,7};   // . (punto)             -> . / 
            case 0x4A:                 // - _  (tasto dopo il punto, FIX: prima mancava!)
                return shiftHeld ? CpcKeyPos{4,0}   // Shift+- -> cella "0" -> CPC produce "_"
                                 : CpcKeyPos{3,1};   // -       -> cella "-/=" -> CPC produce "-"
            case 0x4E: return {3,6};   // ' ?  (tasto dopo lo 0)-> / / ?   (Shift = "?")
            case 0x55: return {3,0};   // ì ^  (tasto dopo ')   -> ^ / £   (Shift = "^")
            case 0x54: return {2,1};   // è é  (AltGr = [)      -> [ / {
            case 0x5B: return {3,5};   // + *  (tasto dopo è)   -> : / *   (Shift = "*", utile per moltiplicazioni)
            case 0x4C: return {3,2};   // ò ç  (AltGr = @)      -> @ / |   (Shift = "|", utile per @)
            case 0x52: return {-1,-1}; // à ° - nessun match soddisfacente, lascia libero
            case 0x5D: return {2,6};   // ù §                   -> \ / `
            case 0x61: return {-1,-1}; // tasto ISO extra < > - ridondante (Shift+, e Shift+. gia' danno > e <)
#else
            // =========== LAYOUT TASTIERA FISICA US/EN ===========
            case 0x0E: return {2,6}; // `  ~   -> \ / `
            case 0x4E:                 // -  _  (FIX: stesso trucco Shift usato per IT)
                return shiftHeld ? CpcKeyPos{4,0}   // Shift+- -> cella "0" -> CPC produce "_"
                                 : CpcKeyPos{3,1};   // -       -> cella "-/=" -> CPC produce "-"
            case 0x55: return {3,0}; // =  +   -> ^ / £
            case 0x54: return {2,1}; // [  {   -> [ / {
            case 0x5B: return {2,3}; // ]  }   -> ] / }
            case 0x5D: return {3,2}; // \  |   -> @ / |
            case 0x4C: return {3,4}; // ;  :   -> ; / +
            case 0x52: return {5,1}; // '  "   -> 7 / '  (condivide il tasto CPC del 7)
            case 0x4A: return {3,6}; // /  ?   -> / / ?
#endif
            default:   return {-1,-1};
        }
    }
}

static bool ps2Extended = false;
static bool ps2Break    = false;

inline void keyboardInit() {
    ps2Kbd.begin();   // configura IRQ su PS2_CLK_PIN e avvia la ricezione
}

inline void setCpcKey(int8_t row, int8_t bit, bool pressed) {
    if (row < 0 || row > 9) return;
    if (row == JOY_ROW) {
        if (pressed) kbdRow6Mask &= ~(1 << bit);
        else         kbdRow6Mask |=  (1 << bit);
        keyMatrix[JOY_ROW] = kbdRow6Mask & joyRow6Mask;
    } else {
        if (pressed) keyMatrix[row] &= ~(1 << bit);
        else         keyMatrix[row] |=  (1 << bit);
    }
}

// Da chiamare frequentemente nel loop() di core0: svuota il buffer
// scancode riempito in background dall'ISR di PS2Keyboard.
inline void keyboardTask() {

    while (ps2Kbd.available()) {
        uint8_t code = ps2Kbd.read();
        //Serial.println(code);
        if (code == 0xE0) { ps2Extended = true; continue; }
        if (code == 0xF0) { ps2Break = true; continue; }

        // Aggiorna lo stato Shift PRIMA di tradurre, cosi' il tasto
        // "-"/"_" (e in generale eventuali altri casi ambigui futuri)
        // vede lo stato corretto.
        if (!ps2Extended && (code == 0x12 || code == 0x59)) {
            ps2ShiftDown = !ps2Break;
        }

        CpcKeyPos pos = translateScancode(code, ps2Extended, ps2ShiftDown);
        ps2Extended = false;

        if (pos.row >= 0) setCpcKey(pos.row, pos.bit, !ps2Break);
        ps2Break = false;
    }
}