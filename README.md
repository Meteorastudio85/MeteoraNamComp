# Meteora NAM Bus Compressor — Meteora Plugins

Compressore da bus stereo in stile SSL. Il suono viene dalle catture NAM del
**GSSL Bus Gold Compressor 2017** (14 modelli, canali L/R separati).

## Come funziona

| Modalità | Catena |
|---|---|
| **Hybrid** (default) | compressore VCA in DSP (sidechain stereo collegato) → Make-Up → modello NAM *NoCompression* (colore analogico dei circuiti) |
| **Capture** (LED CAPTURE acceso) | Threshold usato come drive → cattura NAM del compressore vero → compensazione → Make-Up |

In **Capture** la cattura viene scelta così:
- Ratio → 2 / 4 / 10
- Release **AUTO** → cattura "A-10 / R-Auto" (veloce)
- Release su qualsiasi altro valore → cattura "A-30 / R-100" (lenta)
- Attack non ha effetto, perché le catture hanno tempi fissi.

## Controlli

- **Threshold**: da −15 a +15 dB (0 = −12 dBFS)
- **Ratio**: 2 / 4 / 10
- **Attack**: .1 / .3 / 1 / 3 / 10 / 30 ms
- **Release**: .1 / .3 / .6 / 1.2 s / AUTO (rilascio dipendente dal programma)
- **Make-Up Gain**: da 0 a +15 dB
- **Mix**: compressione parallela, DRY → WET (default 100% WET)
- **VU meter con vetro** (riflessi e ombra del bordo sopra la lancetta)
- **VU**: commuta la lancetta tra riduzione di gain (scala inferiore) e livello d'uscita (scala superiore, 0 VU = −18 dBFS RMS)
- **Power**: bypass senza click; la latenza resta costante
- **LED rosso**: clip in uscita
- **CAPTURE / ECO**: modalità cattura originale e rete neurale leggera (meno CPU)
- Doppio clic su una manopola la riporta al valore di default; la finestra si può ridimensionare.

## Compilazione (GitHub Actions)

1. Carica il contenuto di questa cartella in un repository GitHub.
2. Il workflow `.github/workflows/build.yml` parte da solo a ogni push e compila:
   - **Windows**: VST3
   - **macOS** (Universal: Apple Silicon + Intel): VST3 e AU
3. Scarica i plugin da **Actions → ultima build → Artifacts**.

## Installazione

- **Windows**: copia `Meteora NAM Bus Compressor.vst3` in `C:\Program Files\Common Files\VST3\`
- **macOS**:
  - copia il `.vst3` in `~/Library/Audio/Plug-Ins/VST3/`
  - copia il `.component` in `~/Library/Audio/Plug-Ins/Components/`
  - il plugin non è firmato da Apple, quindi la prima volta esegui nel Terminale:
    `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Meteora NAM Bus Compressor.vst3" ~/Library/Audio/Plug-Ins/Components/"Meteora NAM Bus Compressor.component"`

## Note tecniche

- I modelli lavorano a 48 kHz. Con altre frequenze di campionamento il plugin ricampiona internamente (sinc a 32 tap) e comunica la latenza alla DAW, che la compensa da sola:
  - 48 kHz: 16 campioni
  - 44,1 kHz: 57 campioni
  - 96 kHz: 110 campioni
- Le catture hanno un ritardo proprio di 16 campioni. Il plugin lo compensa, quindi il Mix non crea filtraggi a pettine.
- Il motore WaveNet è scritto da zero (`Source/dsp/NamWaveNet.h`). È stato verificato campione per campione contro un'implementazione di riferimento (errore circa −133 dB).
- CPU: la rete piena (8 canali) pesa circa il 10% di un core per canale su una CPU da 2 GHz. **ECO** (3 canali) scende a circa il 2–3%.
- `tools/make_assets.py` (Python + OpenCV) rigenera la grafica in `Resources/` partendo da `tools/panel_source.jpg`: toglie dalla tua immagine gli indicatori delle manopole e la lancetta, che il plugin disegna dal vivo.

## Crediti

Catture NAM: "GSSL Bus Gold Compressor 2017" di *logicleopard* (TONE3000, id 384618).
Prima di distribuire il plugin pubblicamente, verifica le condizioni d'uso delle catture.
