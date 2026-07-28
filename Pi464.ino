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
 *  ============================================================
 *  Emulatore Amstrad CPC464 per Raspberry Pi Pico
 *   - CPU: Z80 di PiMSX (github.com/ivanvddr/PiMSX), PATCHATA per
 *    usare porte I/O a 16 bit (vedi Z80.h/Z80.cpp, cerca "MODIFICA CPC")
 *  - Video: ILI9341 320x240 via TFT_eSPI + DMA (core1)
 *  - Audio: AY3891 Usa PWM su GPIO15 con IRQ diretta. 
 *     IRQ PWM_IRQ_WRAP viene abilitata su core0 (via
 *    ay.begin() chiamata in setup), core0 rialimenta anche il ring
 *    buffer chiamando ay.update() nel loop - stessa architettura
 *     "generazione su core0 / interrupt servita su core0"
 *   - Tastiera: PS/2 (classe PS2Keyboard)
 *   - Joystick: 5 vie + 2 pulsanti passivi
 *  - Disco: due metodi disponibili, selezionabili con USE_FDC_AMSDOS:
 *       0 = Trap HLE veloce (bypassa CAS_IN_*, niente FDC/AMSDOS reali)
 *       1 = FDC reale (uPD765) + ROM AMSDOS vera (richiede amsdosrom.h)
 *    I due metodi NON possono essere attivi contemporaneamente: il
 *    codice di entrambi resta nello sketch, ma solo uno e' agganciato
 *    a ioRead()/ioWrite()/loop() a seconda dello switch.					 
 *  ============================================================
 */
#include <TFT_eSPI.h>
#include "config.h"
#include "memory.h"
#include "gatearray.h"
#include "ppi8255.h"
#include "video.h"
#include "AY3891.h"
#include "keyboard_ps2.h"
#include "joystick.h"
#include "Z80.h"
#include "perf.h"
#include "PS2Keyboard.h"
#include "CPCLogo.h"
#include "CPCMenu.h"
#include "CPCGameBrowser.h"
#include "timing.h"
#include "storage.h"
#include "debug_littlefs.h"
// ---- Metodo Trap HLE (veloce, niente FDC/AMSDOS reali) ----
#include "dsk.h"
#include "hle_cas.h"
#include "autorun.h"
// ---- Metodo FDC reale + AMSDOS ----
#include "dsk_sectors.h"
#include "fdc.h"
// overclock
#include "hardware/vreg.h"    // Per vreg_set_voltage
#include "hardware/clocks.h"  // Per clock_configure e clk_peri

enum AppState {
    STATE_MENU,
    STATE_BASIC,
    STATE_GAME
};

AppState currentState = STATE_MENU;

CPCMenu* menu = nullptr;
CPCGameBrowser* gameBrowser = nullptr;

PerfStats perf;

Z80 z80;
TFT_eSPI tft = TFT_eSPI();
PS2Keyboard ps2Kbd(PS2_DATA_PIN, PS2_CLK_PIN);
AY3891 ay;

// Disco per il metodo FDC reale (quello per l'HLE, "currentDsk", e'
// gia' dichiarato extern in dsk.h e definito in dsk.cpp)
DskImage2 gameDisk;																	  
static uint32_t tstateAccum = 0;
volatile bool firmwareReady = false;   // accessibile anche da video.h
volatile bool sysClocksReady = false;   //garantisce che tft.init venga eseguito solo dopo l'overclock del bus spi

// Matrice tastiera CPC: 10 righe x 8 bit (attivo basso = tasto premuto).
// Definizione reale (vedi keyboard_matrix.h per la dichiarazione extern
// condivisa con keyboard_ps2.h, joystick.h e AY3891.cpp).
volatile uint8_t keyMatrix[10] = {0xFF,0xFF,0xFF,0xFF,0xFF,
                                   0xFF,0xFF,0xFF,0xFF,0xFF};	
volatile uint8_t cpcKeyboardRowSel = 0;
volatile uint8_t kbdRow6Mask = 0xFF;
volatile uint8_t joyRow6Mask = 0xFF;	


void cleanupTFT() {
    Serial.println("Cleanup TFT...");
    
    tft.endWrite();
    
    uint32_t timeout = millis();
    while (tft.dmaBusy() && (millis() - timeout < 200)) {
        delay(1);
    }
    
    if (tft.dmaBusy()) {
        Serial.println("TFT DMA force stop!");
        // TFT_eSPI usa di default channel 0 e 1
        dma_channel_abort(0);
        dma_channel_abort(1);
    }
    
    tft.fillScreen(TFT_BLACK);
    delay(50); // aspetta che fillScreen finisca
    
    tft.setViewport(0, 0, 320, 240);
    tft.setTextDatum(TL_DATUM);
    
    Serial.println("TFT clean");
}

void resetAllDMA() {
    Serial.println("Reset DMA channels...");
    
    // ferma tutte le transazioni DMA attive
    for (int ch = 0; ch < 12; ch++) {
        dma_channel_abort(ch);
        dma_channel_wait_for_finish_blocking(ch);
        
        // libera se era claimed
        if (dma_channel_is_claimed(ch)) {
            dma_channel_unclaim(ch);
        }
    }
    
    // reset hardware DMA controller
    hw_clear_bits(&dma_hw->ch[0].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    
    delay(10);
    Serial.println("DMA reset complete");
}
															 
// ---- Decodifica indirizzi I/O CPC464 -------------------------------
// bit15,14 del bus indirizzi selezionano il device (schema classico CPC):
//   01 -> Gate Array   (0x4000-0x7FFF, scrittura)
//   10 -> CRTC 6845    (0x8000-0xBFFF)
//   11 -> PPI 8255     (0xC000-0xFFFF)
//
// Le porte FDC (&FB7E/&FB7F) e la selezione ROM alta (&DFxx) sono
// controllate ESPLICITAMENTE prima dello switch grezzo hi/sel, perche'
// con la decodifica a 2 bit usata qui sotto cadrebbero nel branch
// sbagliato (es. &DFxx avrebbe hi=3 e verrebbe scambiata per PPI).
// Restano attive sempre, indipendentemente da USE_FDC_AMSDOS: in
// modalita' Trap HLE semplicemente non vengono mai toccate dal
// firmware (le CAS_IN_* sono intercettate ben prima di arrivare qui),
// quindi non interferiscono.																 
uint8_t ioRead(uint16_t port) {
    // ---- FDC: Main Status Register (&FB7E) ----
    if (port == 0xFB7E) {
        return fdc.readMSR();
    }
    // ---- FDC: registro dati (&FB7F) ----
    if (port == 0xFB7F) {
        return fdc.readData();
    }						 
    uint8_t hi = (port >> 14) & 0x03;
    uint8_t sel = (port >> 8) & 0x03;
    switch (hi) {
        case 2: // CRTC
            if (sel == 3) return crtcReadData();
            return 0xFF;
        case 3: // PPI
            switch (sel) {
                case 0: return ppiReadPortA();
                case 1: return ppiReadPortB();
                case 2: return ppiReadPortC();
                default: return 0xFF;
            }
        default:
            return 0xFF;
    }
}

void ioWrite(uint16_t port, uint8_t val) {
    // ---- Selezione ROM alta (porta ufficiale &DFxx, bit13=0) ----
    if ((port & 0x2000) == 0) {
        romSelectWrite(val);
        return;
    }
    // ---- FDC: controllo motore (&FA7E), ignorato: non serve motion reale ----
    if (port == 0xFA7E) {
        return;
    }
    // ---- FDC: registro dati (&FB7F) ----
    if (port == 0xFB7F) {
        fdc.writeData(val);
        return;
    }																				
    uint8_t hi  = (port >> 14) & 0x03;
    uint8_t sel = (port >> 8) & 0x03;
    switch (hi) {
        case 1: // Gate Array
            gateArrayWrite(val);
            break;
        case 2: // CRTC
            if (sel == 0) crtcSelectReg(val);
            else if (sel == 1) crtcWriteData(val);
            break;
        case 3: // PPI
            switch (sel) {
                case 0: ppiWritePortA(val); break;
                case 1: ppiWritePortB(val); break;   // ora gestita!
                case 2: ppiWritePortC(val); break;
                case 3: ppiWriteControl(val); break;
            }
            break;
        default:
            break;
    }
}


// Esegue 'steps' istruzioni Z80, gestendo il CRTC e gli interrupt
void runCpuSteps(uint32_t steps) {
    for (uint32_t i = 0; i < steps; i++) {
        int cycles = z80.step();
        tstateAccum += cycles;
        while (tstateAccum >= 256) {
            tstateAccum -= 256;
            crtcTickScanline();
        }
        if (gaIntPending) {
            gaIntPending = false;
            z80.triggerINT();
        }
    }
}

void setup() {
    Serial.begin(115200);

    //overclock
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    set_sys_clock_khz(PICO_CLOCK_KHZ, true);
    clock_configure(
      clk_peri,
      0, // Nessun glitchless mux
      CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // Sorgente: System PLL
      PICO_CLOCK_KHZ * 1000, // Frequenza di ingresso (276 MHz)
      PICO_CLOCK_KHZ * 1000  // Frequenza richiesto in uscita (276 MHz)
    );		

    tft.init();
    tft.initDMA();
    tft.setRotation(1); // 320x240
    tft.setSwapBytes(true);

    tft.fillScreen(TFT_BLACK);

    joystickInit();

    menu = new CPCMenu(&tft);
    
    //Imposta logo
    menu->setLogo(logoPiCPC, 143, 70);
    
    // Inizializza menu
    menu->begin();
    
    Serial.println("Setup completed!");
}

void handleMenu() {
    MenuAction action = menu->update(); 
    
    switch (action) {
        case ACTION_START_BASIC:
            Serial.println("BASIC start...");
            currentState = STATE_BASIC;
            break;
            
        case ACTION_START_GAME:
            Serial.println("GAME menu start...");
            currentState = STATE_GAME;
            break;

        case ACTION_NONE:
        default:
            // Nessuna azione
            break;
    }
}

void handleBasic() {
  SPI1.end();
  delay(100);
  resetAllDMA();
  cleanupTFT();
  delay(100);
  CPCsetup();
  CPCloop();
}

void handleGameBrowser() {
    // Crea browser solo una volta
    if (!gameBrowser) {
        
        // Crea browser (passa il filesystem dal Loader)
        gameBrowser = new CPCGameBrowser(&tft);
        gameBrowser->setLogo(logoPiCPC_small, 61, 30);
        gameBrowser->setPath("/disks");
        gameBrowser->setFileExt(".dsk");
        
        if (!gameBrowser->begin()) {
            Serial.println("Browser init failed!");
            delete gameBrowser;
            gameBrowser = nullptr;
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Browser OK, libera menu
        delete menu;
        menu = nullptr;
        Serial.printf("Browser active (%s)\n", storageName() );
    }
    
    uint8_t joyState = joystickReadHardware();
    // Aggiorna browser
    bool gameSelected = gameBrowser->update(joyState);
    
    if (gameSelected) {
        Serial.println("Disk selected!");
        
        // Browser recupera il path
        String diskPath = gameBrowser->getDiskSelected();
        
        // ========================================
        // STEP 1: Libera browser
        // ========================================
        delete gameBrowser;
        gameBrowser = nullptr;
        Serial.println("Browser destroyed");
        
        
        // ========================================
        // STEP 3: Reset hardware
        // ========================================
        resetAllDMA();
        cleanupTFT();
        delay(100);
        
        // ========================================
        // STEP 4: Setup CPC
        // ========================================
        CPCsetup();
        
        // ========================================
        // STEP 5: Carica gioco con Loader!
        // ========================================
        loadDisk(diskPath.c_str());
        
        Serial.println("disk loaded!");
        
        CPCloop();
    }
    
    delay(10);
    yield();
}

void loop() {
      switch (currentState) {
        case STATE_MENU:
            handleMenu();
            break;
            
        case STATE_BASIC:
            handleBasic();
            break;
            
        case STATE_GAME:
            handleGameBrowser();
            break;
      } 
}

void loadDisk(const char* dsk) {
    // ---- Montaggio disco e autorun ----
    // Fatto DOPO il warm-up: il disco viene "inserito" a firmware gia'
    // avviato, esattamente come inserire un floppy in un CPC gia' acceso.
    Serial.printf("[Main] Storage attivo: %s\n", storageName());

    #if USE_FDC_AMSDOS
            // ---- Metodo FDC reale + AMSDOS ----
            if (gameDisk.mount(dsk)) {
                fdc.insertDisk(&gameDisk);
                Serial.println("[Main] Disco inserito nell'FDC. Dal BASIC: RUN\"nomefile o "
                                "comandi |disco, a seconda di come il firmware seleziona AMSDOS.");
            } else {
                Serial.printf("[Main] Impossibile montare %s\n", dsk);
            }
    #else
            // ---- Metodo Trap HLE veloce ----
            if (currentDsk.mount(dsk)) {
                if (!autorunBoot(z80)) {
                    Serial.println("[Main] Autorun non riuscito: il disco resta montato; "
                                    "l'HLE gestira' comunque un RUN\"nomefile digitato a mano");
                }
            } else {
                Serial.printf("[Main] Impossibile montare %s, si parte dal prompt BASIC\n", DSK_FILE_PATH);
            }
    #endif
}

void CPCsetup() {

    //sync start core1 work
    sysClocksReady = true;

    memoryInit();
    crtcInit();            // stato iniziale CRTC (registri di default, MA, contatori)
    keyboardInit();

    z80.begin(memRead, memWrite, ioRead, ioWrite);
    z80.reset();
    z80.setPC(0x0000);          // il firmware CPC parte da 0x0000 (ROM inferiore)
    z80.setInterruptMode(1);    // il CPC lavora sempre in IM1 (RST 0x38 su INT)

    // --- Esegui il warm-up del firmware ---
    // Il firmware CPC464 impiega circa 100.000 istruzioni per arrivare al prompt BASIC.
    // Uso 1.000.000 per essere sicuro.
    runCpuSteps(1000000);
    firmwareReady = true;   // ora il rendering può partire
    ay.begin();
    timingInit(); 

}

void CPCloop() {

      while(true) {
          
          int cycles;			

          #if USE_FDC_AMSDOS
            // ---- Metodo FDC reale: nessuna intercettazione, il firmware/AMSDOS
            //      reale dialoga con l'FDC tramite le porte I/O normali ----
            //uint32_t t0 = micros();
            cycles = z80.step();
            //perf.cpuBusyUs += micros() - t0;
          #else
            // ---- Metodo Trap HLE veloce ----
            if (hleCasIntercept(z80)) {
              // L'istruzione a questo indirizzo (CALL a una routine firmware
              // CAS_IN_*) e' stata gestita direttamente dalla nostra logica
              // C++ invece che dal vero codice Z80/ROM: addebitiamo comunque
              // un numero plausibile di T-state per non alterare troppo il
              // timing di CRTC/interrupt (una CALL+RET reale costerebbe circa
              // 10-40 T-state; il valore esatto non e' critico qui).
              cycles = 40;
            } else {
              cycles = z80.step();    // esegue una istruzione, ritorna i T-state consumati
            }
          #endif

          tstateAccum += (uint32_t)cycles;	

          //uint32_t t0 = micros(); 
          //int cycles = z80.step();    // esegue una istruzione, ritorna i T-state consumati
          //tstateAccum += (uint32_t)cycles;
          //perf.cpuBusyUs += micros() - t0;

          // Il CRTC del CPC gira a 1MHz (4x piu' lento della CPU): una
          // scanline dura 64us = 64 tick CRTC = 256 T-state Z80 a 4MHz.
          // Avanziamo la macchina a stati CRTC (timing verticale, interrupt,
          // indirizzamento video) una scanline alla volta.
          while (tstateAccum >= 256) {
              tstateAccum -= 256;
              crtcTickScanline();
              timingThrottleScanline();
          }

          if (gaIntPending) {
              gaIntPending = false;
              z80.triggerINT();   // in IM1 il vettore e' ignorato (salta sempre a 0x0038)
          }

          keyboardTask();
          joystickTask();
          //uint32_t ta = micros();    
          ay.update();
          //perf.audioBusyUs += micros() - ta;

          //perfMaybeReport(); 
      } 
}

// ============================================================
//  CORE 1: aggiornamento display (DMA) 
// ============================================================
void setup1() {
    //wait for sys overclock
    while (!sysClocksReady) {
        tight_loop_contents();   // busy-wait leggero
    }
    videoInit();
	  //ay.begin();
}

void loop1() {
    renderFrame();
}
