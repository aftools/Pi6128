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
 
#include "PS2Keyboard.h"

PS2Keyboard* PS2Keyboard::_instance = nullptr;

PS2Keyboard::PS2Keyboard(uint8_t dataPin, uint8_t clockPin)
    : _dataPin(dataPin), _clockPin(clockPin),
      _head(0), _tail(0), _bitCount(0), _shiftReg(0), _parity(0)
{
    _instance = this;
    memset((void*)_buffer, 0, PS2_BUFFER_SIZE);
}

void PS2Keyboard::begin() {
    // Configura pin come input con pull-up (open-collector PS/2)
    gpio_init(_dataPin);
    gpio_set_dir(_dataPin, GPIO_IN);
    gpio_pull_up(_dataPin);

    gpio_init(_clockPin);
    gpio_set_dir(_clockPin, GPIO_IN);
    gpio_pull_up(_clockPin);

    // Registra ISR sul fronte di discesa del clock
    gpio_set_irq_enabled_with_callback(
        _clockPin,
        GPIO_IRQ_EDGE_FALL,
        true,
        &PS2Keyboard::gpioISR
    );

    Serial.printf("PS/2: data=%d clock=%d\n", _dataPin, _clockPin);
}

void PS2Keyboard::gpioISR(uint gpio, uint32_t events) {
    if (_instance) _instance->handleBit();
}

void PS2Keyboard::handleBit() {
    uint8_t bit = gpio_get(_dataPin) ? 1 : 0;

    if (_bitCount == 0) {
        // Start bit: deve essere 0
        if (bit == 0) {
            _shiftReg = 0;
            _parity   = 0;
            _bitCount = 1;
        }
    } else if (_bitCount >= 1 && _bitCount <= 8) {
        // Bit dati (LSB first)
        _shiftReg |= (bit << (_bitCount - 1));
        _parity   ^= bit;
        _bitCount++;
    } else if (_bitCount == 9) {
        // Bit di parità (odd parity: XOR di tutti i bit dati + parità bit == 1)
        // Non blocchiamo se parità sbagliata, ma potremmo aggiungere un flag errore
        _bitCount++;
    } else if (_bitCount == 10) {
        // Stop bit: deve essere 1
        if (bit == 1) {
            // Byte valido → inserisci nel buffer
            uint8_t next = (_head + 1) % PS2_BUFFER_SIZE;
            if (next != _tail) {
                _buffer[_head] = _shiftReg;
                _head = next;
            }
            // else: buffer pieno, byte perso
        }
        _bitCount = 0;
    }
}

bool PS2Keyboard::available() {
    return _head != _tail;
}

uint8_t PS2Keyboard::read() {
    if (_head == _tail) return 0;
    uint8_t val = _buffer[_tail];
    _tail = (_tail + 1) % PS2_BUFFER_SIZE;
    return val;
}
