

#pragma once
#include <Arduino.h>

// ============================================================
//  ROM AMSDOS (16384 byte totali)
// ============================================================
//
// NON posso includere il dump reale della ROM (e' materiale
// protetto da copyright). Devi generare questo file a partire da un
// TUO dump legale (amstrad amsdos.rom).
//
// Procedura consigliata (da PC, non sul Pico):
//   1) Procurati il file amsdos.rom
//   2) Convertilo in header C con questo script Python:
//
//        with open("amsdos.rom","rb") as f:
//            data = f.read()
//        with open("cpc464rom.h","w") as out:
//            out.write("#pragma once\n#include <Arduino.h>\n")
//            out.write(f"static const uint8_t amsdosRom[{len(data)}] = {{\n")
//            for i,b in enumerate(data):
//                out.write(f"0x{b:02X},")
//                if i % 16 == 15: out.write("\n")
//            out.write("\n};\n")
//
//   3) Sostituisci questo file con quello generato.
//
// Sotto trovi uno STUB a zero SOLO per permettere la compilazione:
// Non potra leggere e avviare immagini disco .dsk in modalitá FDC+AMSDOS reali finche' non sostituisci questo file.


const uint8_t amsdosRom[16384] = {
  0x00 
};