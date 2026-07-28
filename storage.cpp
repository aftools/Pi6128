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
#include "storage.h"

// Unica, vera definizione della variabile globale di stato dello
// storage attivo: condivisa da tutte le unita' di compilazione che
// includono storage.h (il .ino, dsk.cpp, ecc.)
StorageBackend activeStorage = StorageBackend::NONE;

char selectedDiskPath[128];  // Path del gioco selezionato