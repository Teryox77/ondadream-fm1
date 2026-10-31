# OndaDream — firmware per M-VAVE FM-1

**OndaDream** è un firmware per il sintetizzatore tascabile **M-VAVE FM-1**, nato dal synth web OndaDream di Teryox77.
È un fork di **[Felucca](https://github.com/hugelton/Felucca)** di Leo Kuroshita (Hügelton Instruments), da cui eredita
motori sonori, sequencer, effetti, installer ed editor.

- **Provalo nel browser:** https://teryox77.github.io/ondadream-fm1/webapp/try/
- **Installalo sull'FM-1** (Chrome o Edge, cavo USB dati): https://teryox77.github.io/ondadream-fm1/webapp/installer/
- **Scarica il pacchetto `.fwsc`:** [release "latest"](https://github.com/Teryox77/ondadream-fm1/releases/tag/latest)

## Cosa c'è nella v0.1

- I **42 suoni di OndaDream** come primi preset del motore ANALOG (poi quelli originali di Felucca).
- **4 tracce all'accensione**: PAD SOGNO, BASSO CALDO, PLUCK (ANALOG) e la **batteria 808**, ognuna col suo pattern: premi PLAY.
- La **creatura** sullo schermo HOME al posto dell'oscilloscopio: diversa per ogni suono, reagisce alle note.
- Schermata di avvio OndaDream.

## Tornare indietro

L'installer di Felucca/OndaDream ha "Return to official V15", oppure usa l'updater ufficiale M-VAVE.
Installare firmware personalizzati è a tuo rischio.

## Compilazione

Ogni push su `main` compila tutto su GitHub Actions (`.github/workflows/build.yml`): il pacchetto con la toolchain
JieLi, l'emulatore (`tools/onda/build_emu.sh`, clang + WASI) e il sito su GitHub Pages.
In locale: vedi [BUILDING.md](BUILDING.md) di Felucca. Senza toolchain JieLi: `tools/onda/gen.sh` e poi
`tools/onda/build_emu.sh` per l'emulatore.

Modifiche rispetto a Felucca: `firmware/src/creature.c` (nuovo), `firmware/src/ui_graph.c` (HOME),
`firmware/engines/src/eng_analog.c` (preset), `firmware/src/engines.c` (tracce all'accensione),
`firmware/src/ui_draw.c` (splash), `web/emu/index.html`, `tools/onda/`.

## Licenza

GPL-3.0, come Felucca (vedi [LICENSE](LICENSE), [LICENSING.md](LICENSING.md), [CREDITS.md](CREDITS.md)).
Il README originale di Felucca è in [README-FELUCCA.md](README-FELUCCA.md).
M-VAVE e FM-1 sono marchi dei rispettivi proprietari; OndaDream e Felucca non sono affiliati con loro.
