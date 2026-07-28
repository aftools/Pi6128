#pragma once
#include <Arduino.h>

// ============================================================
//  ROM CPC464 (32768 byte totali)
//    [0x0000-0x3FFF] = OS ROM (lower rom)
//    [0x4000-0x7FFF] = BASIC 1.0 ROM (upper rom)
// ============================================================
//
// NON posso includere il dump reale della ROM Amstrad (e' materiale
// protetto da copyright). Devi generare questo file a partire da un
// TUO dump legale (es. estratto da un CPC464 che possiedi).
//
// Procedura consigliata (da PC, non sul Pico):
//   1) Procurati i due file "OS_464.ROM" (16384 byte) e
//      "BASIC_1.0.ROM" (16384 byte) dal tuo CPC464.
//   2) Concatenali in un unico file da 32768 byte:
//        cat OS_464.ROM BASIC_1.0.ROM > cpc464.rom
//   se giá disponi del file rom da 32kb che include entrambi (cpc464.rom) i primi due step non sono necessari
//   3) Convertilo in header C con questo script Python:
//
//        with open("cpc464.rom","rb") as f:
//            data = f.read()
//        with open("cpc464rom.h","w") as out:
//            out.write("#pragma once\n#include <Arduino.h>\n")
//            out.write(f"static const uint8_t cpc464rom[{len(data)}] = {{\n")
//            for i,b in enumerate(data):
//                out.write(f"0x{b:02X},")
//                if i % 16 == 15: out.write("\n")
//            out.write("\n};\n")
//
//   4) Sostituisci questo file con quello generato.
//
// NOTA: essendo l'array in flash del Pico (che e' gia' mappata
// in modo lineare via QSPI XIP), puoi leggerlo come
// normale array "static const uint8_t[]": sul RP2040 la
// la flash e' accessibile come memoria normale, quindi memRead() puo'
// leggere direttamente cpc464rom[addr] senza pgm_read_byte().
//
// Sotto trovi uno STUB a zero SOLO per permettere la compilazione:
// il Basic NON partira' finche' non sostituisci questo file.

static const uint8_t cpc464rom[32768] = {  
  0x0
};
