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
//  CONFIGURAZIONE HARDWARE
// ============================================================

// ---- Display ILI9341 via TFT_eSPI (pin definiti in User_Setup.h di TFT_eSPI) ----
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// overclock
#define PICO_CLOCK_KHZ 276000

// L'area video CPC utile e' 320x200 (mode 0/1), la centriamo verticalmente
#define CPC_SCREEN_W   320
#define CPC_SCREEN_H   200
#define CPC_SCREEN_Y_OFFSET  ((TFT_HEIGHT - CPC_SCREEN_H) / 2)

// ---- PS/2 ----
#define PS2_DATA_PIN   27
#define PS2_CLK_PIN    28

// SD CARD
#define SD_CS_PIN 13
#define SD_SCLK_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 12

// ---- Joystick digitale a 5 vie + pulsanti ----
#define JOY_UP_PIN     2
#define JOY_DOWN_PIN   3
#define JOY_LEFT_PIN   4
#define JOY_RIGHT_PIN  5
#define JOY_FIRE1_PIN  6
#define JOY_FIRE2_PIN  26

// ---- CPU CPC464 ----
#define CPC_CPU_HZ     4000000UL

#define USE_FDC_AMSDOS 1   // <-- CAMBIA QUI: 1 = FDC+AMSDOS reali, 0 = Trap HLE veloce	

// Frequenza HSYNC CRTC (~15625Hz), un frame dura 1/50s.
// Uso per semplicita' un contatore a T-state per generare
// gli impulsi di interrupt del Gate Array (ogni 52 righe -> 6 volte/frame)
#define TSTATES_PER_FRAME   (CPC_CPU_HZ / 50)   // 80000 T-state per frame (PAL 50Hz)
#define TSTATES_PER_HSYNC   64                  // circa 15625Hz -> 4MHz/15625 = 256... vedi gatearray.h
