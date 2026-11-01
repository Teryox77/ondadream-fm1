# OndaDream — firmware per M-VAVE FM-1

Un sintetizzatore tutto nuovo per l'FM-1: 43 preset, creature animate sullo schermo, pagine FILTER, ENVELOPE, LFO,
ARPEGGIO (con modalità AI), EFFECTS (chorus, eco, riverbero) e GLOBAL, comandate da manopole e tasti dell'FM-1.

- **Provalo nel browser:** https://teryox77.github.io/ondadream-fm1/webapp/try/
- **Installalo sull'FM-1** (Chrome o Edge, cavo USB dati): https://teryox77.github.io/ondadream-fm1/webapp/installer/
- **Il file del firmware:** [release "latest"](https://github.com/Teryox77/ondadream-fm1/releases/tag/latest)

Se dopo l'installazione l'FM-1 non parte, due accensioni fallite di fila lo mettono da solo in modalità di
aggiornamento (UBOOT): da lì si reinstalla dalla stessa pagina, oppure si rimette il firmware originale M-VAVE.

## Comandi

| Controllo | Funzione |
| --- | --- |
| PRESETS | cambia suono (43) |
| SELECT | scorre le pagine |
| ALGORITHM | forma d'onda |
| KNOB 1–4 | i quattro valori della pagina (le card colorate in basso) |
| MASTER | volume |
| HOME · EDIT · ENV · LFO · ARP · FX · GLO | le pagine |
| SCL | HOLD |
| REC | arpeggio AI |
| OCT− / OCT+ | ottava |

SEQ, PLAY e SAVE arrivano con la prossima versione (sequencer a 4 tracce con batteria 808).

## Struttura

| Cartella | Cosa |
| --- | --- |
| `src/core/` | il synth, la grafica, l'interfaccia: lo stesso codice gira sull'FM-1 e nel browser |
| `src/fm1/` | la piattaforma FM-1 (sopra felucca-bsp) |
| `bsp/` | felucca-bsp, lo strato hardware (MPL-2.0) |
| `web/` | l'emulatore nel browser (WebAssembly) |
| `installer/` | la pagina di installazione (da Felucca, GPL-3.0) |
| `tools/` | generatori (tabelle, preset, font) e build |
| `tests/` | test sul computer: suono di tutti i preset, schermate di tutte le pagine |

Build: GitHub Actions (`.github/workflows/build.yml`) con il toolchain JieLi.

## Licenza

© 2026 Teryox77, tutti i diritti riservati ([LICENSE](LICENSE)). Le parti di terzi mantengono le loro licenze
([NOTICE](NOTICE)): felucca-bsp (MPL-2.0), l'installer di Felucca (GPL-3.0), i file dell'SDK JieLi nel pacchetto
(Apache-2.0), i font (SIL OFL 1.1). M-VAVE e FM-1 sono marchi dei rispettivi proprietari.
