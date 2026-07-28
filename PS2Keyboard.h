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
 * Driver PS/2 per Raspberry Pi Pico
 *
 * Usa interrupt sul clock PS/2 per leggere i scancode in modo asincrono.
 *
 * Il protocollo PS/2 è seriale sincrono:
 *  - 1 start bit (0)
 *  - 8 bit dati (LSB first)
 *  - 1 bit parità (odd)
 *  - 1 stop bit (1)
 * Campionamento sul fronte di discesa del clock.
 */

#ifndef PS2KEYBOARD_H
#define PS2KEYBOARD_H

#include <Arduino.h>
#include "hardware/gpio.h"

// Dimensione buffer scancode
#define PS2_BUFFER_SIZE 32

class PS2Keyboard {
public:
    PS2Keyboard(uint8_t dataPin, uint8_t clockPin);

    void    begin();
    bool    available();          // True se c'è un byte nel buffer
    uint8_t read();               // Legge il prossimo byte (scancode raw)

    // Scancode set 2 → key released (preceduto da 0xF0)
    // Scancode con estensione (preceduto da 0xE0)

private:
    uint8_t  _dataPin;
    uint8_t  _clockPin;

    // Buffer circolare (accesso da ISR)
    volatile uint8_t  _buffer[PS2_BUFFER_SIZE];
    volatile uint8_t  _head;
    volatile uint8_t  _tail;

    // Stato macchina seriale PS/2
    volatile uint8_t  _bitCount;
    volatile uint8_t  _shiftReg;
    volatile uint8_t  _parity;

    // ISR statica (singleton pattern per GPIO interrupt)
    static PS2Keyboard* _instance;
    static void gpioISR(uint gpio, uint32_t events);

    void handleBit();
};

#endif // PS2KEYBOARD_H
