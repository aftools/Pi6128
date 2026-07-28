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

#include "AY3891.h"
#include "keyboard_matrix.h"   // MODIFICA CPC: aggancio tastiera su reg. 14 (vedi sotto)

// ============================================================================
// LUT VOLUME LOGARITMICA BASE
// ============================================================================
const uint8_t AY3891::_volLUT[16] = {
//   0     1     2     3     4     5     6     7
     0,    3,    4,    5,    7,   10,   14,   19,
//   8     9    10    11    12    13    14    15
    28,   39,   55,   77,  109,  154,  217,  255
};

AY3891* AY3891::_instance = nullptr;

// ============================================================================
// COSTRUTTORE
// ============================================================================
AY3891::AY3891()
    : _selReg(0), _portA(0xFF), _portB(0xFF),
      _noiseAccum(0), _noiseLFSR(0x1FFFF), _noiseOut(1),
      _envAccum(0), _envCounter(0), _envAttack(0),
      _envHolding(false), _envVol(15), _envLevel(0),
      _levelA(0), _levelB(0), _levelC(0),
      _wIdx(0), _rIdx(0), _pwmSlice(0)
{
    _instance = this;
    memset(_regs,         0,    sizeof(_regs));
    memset((void*)_buf,   0x00, sizeof(_buf));
    memset(_scaledVolLUT, 0,    sizeof(_scaledVolLUT));

    _chA = {0, 1};
    _chB = {0, 1};
    _chC = {0, 1};

    _regs[7]  = 0xFF;
    _regs[14] = 0xFF;
    _regs[15] = 0xFF;
}

// ============================================================================
// BEGIN
// ============================================================================
void AY3891::begin() {
    // Calcola LUT scalata: _scaledVolLUT[i] = _volLUT[i] × AY_MASTER_VOLUME / 765
    // ÷765 = ÷255÷3: normalizza e incorpora la divisione per 3 canali.
    // Range risultante: 0 … AY_MASTER_VOLUME/3 ≤ 85.
    for (int i = 0; i < 16; i++) {
        _scaledVolLUT[i] = (uint8_t)((uint32_t)_volLUT[i] * AY_MASTER_VOLUME / 765);
    }

    reset();

    gpio_set_function(AY_AUDIO_PIN, GPIO_FUNC_PWM);
    _pwmSlice = pwm_gpio_to_slice_num(AY_AUDIO_PIN);

    const uint32_t sysClock = clock_get_hz(clk_sys);
    const uint32_t totalDiv = sysClock / AY_SAMPLE_RATE;
    const uint     divInt   = totalDiv / 256;
    const uint     divFrac  = ((totalDiv % 256) * 16) / 256;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&cfg, divInt, divFrac);
    pwm_config_set_wrap(&cfg, 255);
    pwm_init(_pwmSlice, &cfg, true);
    pwm_set_gpio_level(AY_AUDIO_PIN, 0);

    pwm_clear_irq(_pwmSlice);
    pwm_set_irq_enabled(_pwmSlice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, _pwmISR);
    irq_set_priority(PWM_IRQ_WRAP, 0);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    Serial.printf("[PSG] begin: slice=%u  clkdiv=%u+%u/16  rate≈%lu Hz  "
                  "vol=%d  buf=%d campioni (%.0f ms)\n",
                  _pwmSlice, divInt, divFrac,
                  sysClock / ((divInt * 16 + divFrac) * 16),
                  AY_MASTER_VOLUME, AY_BUF_SIZE,
                  (float)AY_BUF_SIZE / AY_SAMPLE_RATE * 1000.0f);
}

// ============================================================================
// RESET
// ============================================================================
void AY3891::reset() {
    memset(_regs, 0, sizeof(_regs));
    _regs[7]  = 0xFF;
    _regs[14] = 0xFF;
    _regs[15] = 0xFF;
    _selReg   = 0;
    _portA    = 0xFF;
    _portB    = 0xFF;

    _chA = {0, 1};
    _chB = {0, 1};
    _chC = {0, 1};

    _noiseAccum = 0;
    _noiseLFSR  = 0x1FFFF;
    _noiseOut   = 1;

    _envAccum   = 0;
    _envCounter = 0;
    _envAttack  = 0;
    _envHolding = false;
    _envVol     = 15;
    _envLevel   = _scaledVolLUT[15];

    _levelA = _levelB = _levelC = 0;
}

// ============================================================================
// INTERFACCIA Z80
// ============================================================================

void AY3891::selectRegister(uint8_t reg) {
    _selReg = reg & 0x0F;
}

void AY3891::writeData(uint8_t value) {
    switch (_selReg) {
        case 0:  _regs[0] = value;         break;
        case 1:  _regs[1] = value & 0x0F;  break;
        case 2:  _regs[2] = value;         break;
        case 3:  _regs[3] = value & 0x0F;  break;
        case 4:  _regs[4] = value;         break;
        case 5:  _regs[5] = value & 0x0F;  break;
        case 6:  _regs[6] = value & 0x1F;  break;
        case 7:  _regs[7] = value;         break;

        case 8:
            _regs[8] = value & 0x1F;
            if (!(_regs[8] & 0x10)) _levelA = _scaledVolLUT[_regs[8] & 0x0F];
            break;
        case 9:
            _regs[9] = value & 0x1F;
            if (!(_regs[9] & 0x10)) _levelB = _scaledVolLUT[_regs[9] & 0x0F];
            break;
        case 10:
            _regs[10] = value & 0x1F;
            if (!(_regs[10] & 0x10)) _levelC = _scaledVolLUT[_regs[10] & 0x0F];
            break;

        case 11: _regs[11] = value; break;
        case 12: _regs[12] = value; break;

        case 13:
            _regs[13] = value & 0x0F;
            _initEnvelope();
            break;

        case 14:
            _regs[14] = value;
            // MODIFICA CPC: il registro 14 del PSG e' cablato a Port A del PPI,
            // che a sua volta (quando selezionato in lettura tramite BDIR/BC1)
            // restituisce keyMatrix[cpcKeyboardRowSel]. La SELEZIONE della riga
            // avviene pero' altrove, via Port C del PPI (vedi ppi8255.h:
            // ppiWritePortC -> cpcKeyboardRowSel = val & 0x0F), non scrivendo
            // qui: questo case gestisce solo l'eventuale scrittura sul
            // registro 14 stesso (che sulla CPC il firmware non usa mai in
            // scrittura per la tastiera, mantenuto solo per compatibilita'
            // con AY3891::writeData generico).
            break;
        case 15: _regs[15] = value; break;
    }
}

uint8_t AY3891::readData() const {
    if (_selReg == 14) return (cpcKeyboardRowSel < 10) ? keyMatrix[cpcKeyboardRowSel] : 0xFF;
    if (_selReg == 15) return _portB;
    if (_selReg > 15)  return 0xFF;
    return _regs[_selReg];
}

// ============================================================================
// ISR PWM  –  in RAM, latenza minima
// ============================================================================
void __not_in_flash_func(AY3891::_pwmISR)() {
    pwm_clear_irq(_instance->_pwmSlice);
    const uint16_t r = _instance->_rIdx;
    if (r != _instance->_wIdx) {
        pwm_set_gpio_level(AY_AUDIO_PIN, _instance->_buf[r]);
        _instance->_rIdx = (uint16_t)((r + 1) & AY_BUF_MASK);
    }
    // buffer vuoto → duty cycle invariato (silenzio senza click)
}

// ============================================================================
// _stepTone
// ============================================================================
inline void AY3891::_stepTone(ToneCh& ch, uint16_t period) {
    ch.accum += AY_TONE_STEP_Q10;
    const uint32_t thresh = (uint32_t)period << 10;
    while (ch.accum >= thresh) {
        ch.accum -= thresh;
        ch.out ^= 1;
    }
}

// ============================================================================
// _stepNoise  –  LFSR 17-bit (right shift). Taps: bit0 xor bit3.
// equivalente al polinomio x^17 + x^14 + 1 come implementato in openMSX.
// ============================================================================
inline void AY3891::_stepNoise(uint8_t noisePeriod) {
    _noiseAccum += AY_NOISE_STEP_Q10;
    const uint32_t thresh = (uint32_t)noisePeriod << 10;
    while (_noiseAccum >= thresh) {
        _noiseAccum -= thresh;
        const uint32_t fb = (_noiseLFSR ^ (_noiseLFSR >> 3)) & 1;
        _noiseLFSR = (_noiseLFSR >> 1) | (fb << 16);
        _noiseOut  = (uint8_t)(_noiseLFSR & 1);
    }
}

// ============================================================================
// _initEnvelope  –  riavvia il generatore (scrittura su R13)
// ============================================================================
void AY3891::_initEnvelope() {
    _envAccum   = 0;
    _envCounter = 0;
    _envAttack  = (_regs[13] >> 2) & 1;
    _envHolding = false;
    _envVol     = _envAttack ? 0 : 15;
    _envLevel   = _scaledVolLUT[_envVol];
}

// ============================================================================
// _stepEnvelope  –  modello hardware-accurate counter + attack XOR
//
//   volume = _envAttack ? _envCounter : (15 - _envCounter)
//
// Fine sweep (counter == 16):
//   !CONT || HOLD  → hold: counter si ferma a 15
//                    CONT+HOLD+ALT: attack ^= 1 (cambia livello di hold)
//   CONT + !HOLD   → continua: counter torna a 0
//                    ALT: attack ^= 1 (alterna direzione)
// ============================================================================
inline void AY3891::_stepEnvelope() {
    _envAccum += AY_ENV_STEP_Q10;
    uint16_t envPeriod = ((uint16_t)_regs[12] << 8) | _regs[11];
    if (envPeriod == 0) envPeriod = 1;
    const uint32_t thresh = (uint32_t)envPeriod << 10;

    while (_envAccum >= thresh) {
        _envAccum -= thresh;
        if (_envHolding) break;

        _envCounter++;
        if (_envCounter == 16) {
            const uint8_t shape    = _regs[13] & 0x0F;
            const bool    cont     = (shape >> 3) & 1;
            const bool    alt      = (shape >> 1) & 1;
            const bool    hold_bit = (shape >> 0) & 1;

            if (!cont || hold_bit) {
                _envCounter = 15;
                _envHolding = true;
                if (cont && alt) _envAttack ^= 1;
            } else {
                _envCounter = 0;
                if (alt) _envAttack ^= 1;
            }
        }
    }

    _envVol   = _envAttack ? _envCounter : (uint8_t)(15 - _envCounter);
    _envLevel = _scaledVolLUT[_envVol];
}

// ============================================================================
// _mixSample  –  3 canali → 1 byte 8-bit
//
// _scaledVolLUT incorpora già ÷3: somma diretta, nessuna divisione.
// ============================================================================
inline uint8_t AY3891::_mixSample() {
    const uint8_t mix = _regs[7];

    const uint8_t sigA = ((mix & 0x01) ? 1 : _chA.out) & ((mix & 0x08) ? 1 : _noiseOut);
    const uint8_t sigB = ((mix & 0x02) ? 1 : _chB.out) & ((mix & 0x10) ? 1 : _noiseOut);
    const uint8_t sigC = ((mix & 0x04) ? 1 : _chC.out) & ((mix & 0x20) ? 1 : _noiseOut);

    const uint8_t lvA = sigA ? ((_regs[8]  & 0x10) ? _envLevel : _levelA) : 0;
    const uint8_t lvB = sigB ? ((_regs[9]  & 0x10) ? _envLevel : _levelB) : 0;
    const uint8_t lvC = sigC ? ((_regs[10] & 0x10) ? _envLevel : _levelC) : 0;

    return (uint8_t)((uint16_t)lvA + lvB + lvC);
}

// ============================================================================
// _pushSample
// ============================================================================
inline void AY3891::_pushSample(uint8_t s) {
    const uint16_t next = (uint16_t)((_wIdx + 1) & AY_BUF_MASK);
    if (next != _rIdx) {
        _buf[_wIdx] = s;
        _wIdx = next;
    }
}

// ============================================================================
// Riempie il buffer fino a AY_BUF_HIGH_WATER
//
// Strategia anti-glitch:
//   Se il buffer è già >= HIGH_WATER: ritorno immediato (no-op).
//   Altrimenti: produce campioni uno per uno fino a HIGH_WATER o buffer pieno.
//
// Questo permette di chiamare update() SIA una volta per frame (dopo
// pushFrameToDisplay) SIA nel loop di throttle, rimpiazzando completamente
// delayMicroseconds(). Il buffer rimane sempre vicino a HIGH_WATER,
// eliminando gli underrun che causavano i tick periodici.
//
// Periodi tono/rumore letti una volta prima del loop (ottimizzazione invariante).
// ============================================================================
void AY3891::update() {
    // Se il buffer è già sufficientemente pieno: niente da fare
    if (available() >= AY_BUF_HIGH_WATER) return;

    // Periodi: letti una volta, invarianti per questa chiamata
    uint16_t perA = ((uint16_t)(_regs[1] & 0x0F) << 8) | _regs[0];
    uint16_t perB = ((uint16_t)(_regs[3] & 0x0F) << 8) | _regs[2];
    uint16_t perC = ((uint16_t)(_regs[5] & 0x0F) << 8) | _regs[4];
    uint8_t  perN = _regs[6] & 0x1F;

    if (perA == 0) perA = 1;
    if (perB == 0) perB = 1;
    if (perC == 0) perC = 1;
    if (perN == 0) perN = 1;

    // Produce campioni finché non si raggiunge HIGH_WATER
    // available() viene riletto ogni iterazione: l'ISR può consumare nel frattempo
    while (available() < AY_BUF_HIGH_WATER) {
        _stepTone(_chA, perA);
        _stepTone(_chB, perB);
        _stepTone(_chC, perC);
        _stepNoise(perN);
        _stepEnvelope();
        _pushSample(_mixSample());
    }
}
