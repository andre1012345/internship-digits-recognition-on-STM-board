# Riconoscimento di cifre su STM32N6570-DK — architettura, procedura e risultati

*Stato al 19 ottobre 2026 · base per il capitolo sperimentale della tesi*

## 1. Obiettivo

Riconoscere in tempo reale cifre scritte a mano (0–9) inquadrate dalla camera della scheda **STM32N6570-DK**, eseguendo l'intera catena (acquisizione, pre-processing, inferenza, visualizzazione) sul microcontrollore, senza PC né cloud. La rete neurale gira sull'acceleratore hardware **Neural-ART (NPU)** integrato nell'STM32N6.

Repository: `C:\Users\Utente\internship\03_STM32N6570-DK` (GitHub: `andre1012345/STM32N6-digits-recognition`).

## 2. Architettura del sistema

Il sistema è una catena di cinque anelli; ognuno produce l'ingresso del successivo.

### 2.1 Addestramento (PC, Google Colab)

- Notebook: `Training/mnist_cnn_retrain_2026-10.ipynb` (modello attuale, seed 42; stesso training del notebook di giugno `mnist_cnn_augmented.ipynb`). Catena completa in `Training/README.md`.
- Dataset: **MNIST** (60 000 immagini di training, cifre bianche su sfondo nero, 28×28).
- Preparazione: normalizzazione in [0,1], ridimensionamento bilineare a **32×32**, replica su **3 canali** (formato richiesto dalla pipeline camera).
- Data augmentation: rotazione ±15°, traslazione ±10 %, zoom ±15 %, shear 10° (nessuna variazione dello spessore del tratto).
- Rete (CNN):

| Strato | Uscita |
|---|---|
| Input | 32×32×3 |
| Conv2D 16 filtri 3×3, ReLU + MaxPool | 16×16×16 |
| Conv2D 32 filtri 3×3, ReLU + MaxPool | 8×8×32 |
| Conv2D 64 filtri 3×3, ReLU + GlobalAvgPool | 64 |
| Dense 64, ReLU | 64 |
| Dense 10, Softmax | 10 |

- Parametri: **28 394**. Esportazione in ONNX + 300 immagini di calibrazione (`mnist_calib.npz`).

### 2.2 Conversione per la scheda (STEdgeAI)

- **Quantizzazione** per canale a int8 (ST Edge AI Developer Cloud), compilazione in locale con ST Edge AI Core 4.0 (`Training/stedgeai/generate_model.bat`, stessa versione del runtime nel progetto): pesi da 110,9 KB (float32) a **27,6 KB** (int8).
- Compilazione per la NPU Neural-ART: il codice generato e i pesi sono in `Model/`:
  - `network.c`, `stai_network.c/.h` — codice che configura ed esegue la NPU;
  - `network_atonbuf.xSPI2.bin` — pesi, da scrivere nella flash NOR esterna a `0x71000000`.
- Formato I/O: input int8 32×32×3 (scala 1/255, zero point −128); output int8 10 classi (scala 1/256, zero point −128).

### 2.3 Firmware (`Application/Src/main.c`)

Ciclo infinito, circa **15 iterazioni al secondo**:

1. **Acquisizione** — sensore IMX335 → pipeline hardware **DCMIPP**: una pipe porta l'immagine piena al display, l'altra produce un'istantanea **32×32 RGB888** per la rete.
2. **Pre-processing** — luminanza, inversione (inchiostro → chiaro, come MNIST), pulizia dello sfondo, quantizzazione int8 (dettagli e confronto in §6).
3. **Inferenza** — `stai_network_run()` sulla NPU: **≤ 1 ms** per immagine.
4. **Post-processing** — dequantizzazione, classe con punteggio massimo, stampa su UART (`[cifra] confidenza% (ms)`) e sovrimpressione sul display.

Note di implementazione:
- Coerenza cache: invalidare la D-Cache sui buffer scritti dal DMA prima di leggerli; pulirla sul buffer di input prima di avviare la NPU.
- Il buffer di input della rete è riutilizzato dalla NPU come memoria di attivazioni: dopo l'inferenza non contiene più l'immagine (rilevante per il debug, §5).

### 2.4 Memoria e avvio

| Contenuto | Indirizzo (flash NOR esterna) | Note |
|---|---|---|
| Bootloader FSBL (`FSBL/ai_fsbl.hex`) | `0x70000000` | Copia l'applicazione in RAM e la avvia |
| Applicazione (`Project_sign.bin`) | `0x70100000` | Compilata da STM32CubeIDE, firmata automaticamente |
| Pesi del modello | `0x71000000` | Letti direttamente dalla NPU |

Switch di boot:
- **BOOT0 e BOOT1 entrambi a sinistra** → avvio dalla flash (funzionamento normale).
- **BOOT1 a destra** → DEV mode: la scheda attende il programmatore/debugger. Necessaria per scrivere la flash e per il debug.
- La posizione è letta solo all'accensione: dopo ogni cambio, staccare e riattaccare l'USB.

### 2.5 Banco di prova

- Pagina web **MNIST Test Card**: mostra a schermo intero cifre MNIST reali (nero su bianco), una per classe, 5 s di attesa iniziale e poi 10 s per cifra + 2 s di schermo bianco.
- La camera inquadra il monitor a 10–15 cm.
- PuTTY salva l'uscita UART su file; l'istante di arrivo di ogni riga viene campionato sul PC ogni 0,5 s, così ogni predizione è associata alla cifra mostrata (circa 110 frame per cifra).

## 3. Procedura operativa (ripartire da zero)

1. **Compilare** in STM32CubeIDE 2.1.1 il progetto `STM32N6570-DK_GettingStarted_ImageClassification` (`Application/STM32CubeIDE`) → produce `Debug/Project_sign.bin`.
2. Switch in **DEV mode**, riavviare l'alimentazione.
3. PowerShell in `03_STM32N6570-DK`:
   - `Set-ExecutionPolicy -Scope Process Bypass`
   - `.\flash.ps1 -Weights` la prima volta o se la scheda è stata svuotata (FSBL + app + pesi);
   - `.\flash.ps1 -AppOnly` dopo una modifica al solo codice.
4. Switch **entrambi a sinistra**, riavviare l'alimentazione.
5. PuTTY: Serial, COM8, 115200 baud; per i test, sessione salvata "board" con logging *All session output* verso `Doc\logs\` (un file per prova, con la data nel nome).

Avvio corretto su UART: righe `[HW] … OK`, poi il banner `MNIST Digit Classification`, poi le predizioni.

Strumenti installati che servono: STM32CubeIDE 2.1.1, STM32CubeProgrammer 2.22 (usato da `flash.ps1`, con il loader `MX66UW1G45G_STM32N6570-DK`), STEdgeAI 4.0 (solo per riconvertire il modello).

## 4. Problemi incontrati e soluzioni (19/10/2026)

| Sintomo | Causa | Soluzione |
|---|---|---|
| Nessun output, schermo spento | Flash della scheda svuotata da terzi | Riscrittura completa con `flash.ps1 -Weights` |
| UART muta dopo il flash | Switch in DEV mode | Entrambi gli switch a sinistra + riavvio |
| Debugger: "No device found on target" | Switch in modalità avvio da flash (porta di debug chiusa) | BOOT1 a destra + riavvio |
| Progetto "missing .project" in CubeIDE | Registrazione del workspace obsoleta (due versioni di CubeIDE sullo stesso workspace) | Rimozione dal workspace e reimport |
| `CMW_CAMERA_Init` = −7 (nessun sensore rilevato) | Non isolata con certezza: build del mattino non allineata ai sorgenti; cavo camera ricollegato | Ripristino dei sorgenti da git, ricompilazione |
| Predizioni sistematicamente errate | Vignettatura dell'obiettivo + soglia globale nel pre-processing | Sottrazione dello sfondo locale (§6) |

Lezione operativa: un'installazione e un workspace per ciascuno strumento; ripartire sempre dai sorgenti in git e ricompilare prima di flashare.

## 5. Strumento di diagnosi

Nel firmware è stata aggiunta una funzione di debug (`DEBUG_DUMP_MS` in `main.c`): ogni 2 s stampa su UART due immagini 32×32 in ASCII affiancate — **ciò che vede la camera** (scala di grigi invertita) e **ciò che entra nella rete** dopo il pre-processing. Poiché la NPU riutilizza il buffer di input, la seconda immagine viene salvata in una copia (`net_in_dbg`) prima dell'inferenza.

Questo strumento ha permesso di individuare la causa degli errori: confrontare l'input reale con le immagini MNIST attese.

## 6. Pre-processing: dal metodo originale a quello robusto alla vignettatura

### Metodo originale (`PREPROC_MODE 0`)

- `val = (255 − Y) / 255` con `Y = 0,299 R + 0,587 G + 0,114 B`.
- Soglia **globale** al punto medio `(vmin + vmax)/2`: sotto → 0, sopra → riscalato in [0,1].

**Difetto:** l'obiettivo produce **vignettatura** (bordi più scuri del centro). Dopo l'inversione gli angoli scuri sembrano inchiostro e la soglia globale li conserva come macchie grigie. La rete le interpreta come tratti:
- **5 → 2**: la macchia in basso aggiunge la "base" orizzontale del 2;
- **9 → 8**: la macchia chiude un falso occhiello inferiore.

Figura: `Doc/figures/2026-10-19_board_vs_mnist.png` (colonne: MNIST atteso · camera · input della rete).

### Metodo nuovo (`PREPROC_MODE 1`)

Sottrazione dello **sfondo locale**:
1. `val` come sopra.
2. Sfondo stimato pixel per pixel come media su una finestra 13×13 (`PREPROC_BG_RADIUS = 6`), calcolata con immagine integrale: costo O(N), indipendente dalla dimensione della finestra.
3. Residuo `d = val − sfondo`; valori sotto `PREPROC_MIN_CONTRAST = 0,06` → 0.
4. Normalizzazione: `v = min(1, d / (0,7 · d_max))`; se `d_max < 0,05` (scena piatta) l'input resta nero.

La vignettatura, che varia lentamente nello spazio, viene assorbita dallo sfondo; i tratti sottili restano. Figura: `Doc/figures/2026-10-19_preproc_old_vs_new.png`.

## 7. Risultati

Condizioni comuni: cifre MNIST reali (set A) su monitor, ~110 frame per cifra, inferenza ≤ 1 ms.

| Cifra | Test 1 — originale | Test 2 — originale, inquadratura migliore | Test 3 — **nuovo** |
|---|---|---|---|
| 0 | ✗ (→2) | ✓ 96% (conf. 53%) | ✓ 85% (conf. 98%) |
| 1 | ✗ (→2) | ✓ 100% (87%) | ✓ 100% (100%) |
| 2 | ✓ 100% | ✓ 100% (97%) | ✓ 95% (97%) |
| 3 | ✓ 100% | ✓ 100% (98%) | ✓ 100% (100%) |
| 4 | ✓ 100% | ✓ 100% (95%) | ✓ 100% (100%) |
| 5 | ✗ (→2) | ✗ (→2) | **✓ 100% (98%)** |
| 6 | ✗ (→8) | ✓ 100% (88%) | ✓ 98% (92%) |
| 7 | ✗ (→2) | ~ 61% (60%) | ✗ (→3, bordo del monitor nell'inquadratura) |
| 8 | ✓ 100% | ✓ 100% (98%) | ✓ 100% (98%) |
| 9 | ✗ (→3/8) | ✗ (→8) | ~ 52% (69%) |
| **Frame corretti** | **40%** | **75%** | **82%** |

*Percentuali = frame classificati correttamente nella finestra della cifra; tra parentesi la confidenza media sui frame corretti.*

**Test 4 — metodo nuovo con inquadratura corretta** (camera fissata, solo pagina bianca attorno alla cifra): **9 cifre su 10 riconosciute sul 100% dei frame**, confidenza media 92–100%; **89% dei frame corretti** complessivi. Unico errore: il campione del 9 (set A), letto come 8, come nei test 2 e 3. Sullo schermo bianco tra una cifra e l'altra solo il 4% dei frame supera il 70% di confidenza.

Progressione: **40% → 75% → 82% → 89%**.

Osservazioni:
- Modello, quantizzazione e NPU funzionano: quando una cifra è riconosciuta lo è su quasi tutti i frame, con confidenza 90–100%.
- Gli errori del metodo originale erano **sistematici**, quindi dovuti a una distorsione costante dell'input, non a limiti del modello.
- Il metodo originale è molto sensibile all'inquadratura (40% → 75% con lo stesso firmware).
- Con il metodo nuovo la scena vuota produce confidenza bassa (15–20%) invece di predizioni casuali.
- Il 7 del test 3 era un problema di inquadratura: nel test 4 è riconosciuto al 100%.

## 8. Discussione

Il collo di bottiglia non è l'architettura della rete (una CNN di questa taglia raggiunge ~98–99% sul test set MNIST), ma il **domain shift** tra le immagini di training e quelle reali della camera: ottica (vignettatura, fuoco), illuminazione, inquadratura, stile di scrittura. Un convolutional autoencoder non affronterebbe direttamente questo problema: non è un classificatore e, addestrato solo su MNIST, erediterebbe lo stesso divario di dominio.

## 9. Prossimi passi

1. Ripetere il test con i set B e C (più "calligrafie") per capire se l'errore sul 9 dipende dal singolo campione.
2. **Smoothing temporale**: decisione a maggioranza su 5–10 frame consecutivi.
3. **Normalizzazione "alla MNIST"**: ritaglio della cifra, centratura sul baricentro, scala a 20×20 dentro il riquadro.
4. Classe **"nessuna cifra"** nel training.
5. Dataset raccolto con la camera (50–100 immagini per cifra) e **fine-tuning**.
6. Augmentation più realistica (sfocatura, rumore, vignettatura sintetica).
7. Misurare l'accuratezza float vs int8 sul PC per quantificare il costo della quantizzazione.

## 10. Proposta di struttura per il capitolo di tesi

1. Piattaforma: STM32N6, NPU Neural-ART, pipeline camera DCMIPP.
2. Modello e addestramento; quantizzazione e deploy con STEdgeAI.
3. Firmware: ciclo di acquisizione-inferenza, memoria e avvio.
4. Metodologia di test riproducibile (test card + log temporizzato).
5. Analisi degli errori: dal sintomo (errori sistematici) alla causa (vignettatura), con le figure di §6.
6. Pre-processing robusto e risultati comparativi (tabella di §7).
7. Limiti e sviluppi futuri.

## 11. Base di partenza e contributo originale

Il firmware non è stato scritto da zero. Il progetto parte dal repository ufficiale **STMicroelectronics `STM32N6-GettingStarted-ImageClassification`**, versione **v2.3.0** (commit `10c660d`, 16/04/2026; https://github.com/STMicroelectronics/STM32N6-GettingStarted-ImageClassification), adattato tramite studio e *reverse engineering* della sua struttura. Il collegamento al repository originale è conservato in git (`remote upstream`), quindi le differenze sono verificabili con `git diff upstream/main HEAD`.

**Cosa faceva il progetto ST:** classificazione di immagini a colori in **101 classi** (dataset Food-101), con modelli d'esempio EfficientNet-V2 e MobileNet forniti già convertiti, per due schede (STM32N6570-DK e NUCLEO-N657X0-Q). La pipeline hardware della camera (DCMIPP) scriveva l'immagine **direttamente** nel buffer di input della rete: **nessun pre-processing software**.

| Componente | Origine | Stato nel progetto |
|---|---|---|
| Driver HAL/LL, BSP, CMSIS, font e LCD | ST / ARM (Apache-2.0, BSD-3-Clause) | invariati |
| Middleware camera e ISP (`stm32-mw-camera`) | ST (SLA0044) | invariato |
| Runtime NPU `stedgeai-lib` / `ll_aton` | ST (SLA0044) | invariato |
| Bootloader FSBL, programmazione fusibili, crop, interrupt | ST | invariati |
| Configurazione della pipeline camera (`app_camerapipeline.c`) | ST | invariata (riga di diagnostica temporanea poi rimossa) |
| Linker script, documentazione ST (`Doc/ST-reference/`) | ST | invariati / spostati |
| **Modello**: addestramento, export ONNX, quantizzazione int8 e conversione per la NPU | **originale** | notebook `Training/`, file generati in `Model/` (sostituiscono i modelli d'esempio) |
| **Configurazione dell'applicazione** (`app_config.h`: 10 classi, testi) | ST → **modificata** | 145 → 49 righe |
| **`main.c`** | ST → **esteso** | 732 → ~1 200 righe (+511 / −41 rispetto a ST, prima delle modifiche del 22/10) |
| **Script e strumenti**: `flash.ps1`, generatore di `mnist_test.h`, `Training/stedgeai/generate_model.bat`, pagina *MNIST Test Card* | **originali** | — |
| **Riorganizzazione del repository** (rimozione Nucleo e modelli d'esempio, progetto CubeIDE compilabile da un clone) | **originale** | — |
| **Metodologia di test, misure, analisi e documentazione** | **originale** | `Doc/` |

Contributi principali dentro `main.c`:
- buffer intermedio `nn_raw` tra camera e rete, per poter elaborare l'immagine via software;
- **pre-processing** in due varianti (soglia globale; sottrazione dello sfondo locale con immagine integrale e cornice vuota), con parametri configurabili;
- quantizzazione dell'input e dequantizzazione dell'output int8 secondo scala e zero point del modello;
- modalità di test `TEST_MODE 2` (**benchmark** su 500 immagini del test set con tre percorsi, matrici di confusione);
- **cronometraggio per fase** con il contatore di cicli DWT del Cortex-M55;
- **dump diagnostico** dell'input della rete via UART e log strutturato delle predizioni.

Nota sul metodo: comprendere il template (flusso di avvio FSBL → applicazione, firma dell'immagine, pool di memoria della NPU, pipe della camera, gestione delle cache con DMA e NPU) è stato un prerequisito per ogni modifica, ed è un risultato del tirocinio in sé.

## 12. Come si osserva ciò che "vede" la rete (diagnosi visiva)

La scheda non può inviare immagini al PC: l'unico canale disponibile durante il funzionamento è la UART, che trasporta testo. Per osservare l'input della rete è stata aggiunta al firmware una funzione di debug (`DEBUG_DUMP_MS`) che, ogni 2 s, stampa l'immagine 32×32 **come testo**: ogni carattere è un pixel e il simbolo ne codifica la luminosità su 10 livelli.

```
' '   .   :   -   =   +   *   #   %   @
nero (sfondo) ─────────────────────────── bianco (tratto)
```

Vengono stampate affiancate due immagini: a sinistra il fotogramma della camera (scala di grigi invertita), a destra l'input effettivo della rete dopo il pre-processing. Una copia di quest'ultimo viene salvata prima dell'inferenza, perché la NPU riutilizza il buffer di input come memoria di lavoro.

L'immagine non è binaria ("tratto / non tratto") ma in scala di grigi, sia per la camera sia per MNIST: i pixel a cavallo del bordo del tratto sono coperti solo in parte, l'ottica introduce sfocatura e l'illuminazione non è uniforme. La rete è stata addestrata su cifre con bordi sfumati e si aspetta quindi questo tipo di input.

Ricostruendo il testo come immagine si ottiene la figura `Doc/figures/2026-10-22_nine_chain.png`, che mostra il percorso di una cifra lungo la catena: (1) cifra MNIST sullo schermo, (2) fotogramma della camera a 32×32, con vignettatura e sfocatura, (3) input della rete dopo il pre-processing, (4) la stessa cifra come vista in addestramento. Il confronto tra (3) e (4) rende visibile il *domain shift*: tratto più spesso e saturo, occhiello più largo, residui negli angoli.

## 13. Provenienza dei dati e validità dei test

| Test | Immagini | Usate per i pesi? | Altri usi | Valore del risultato |
|---|---|---|---|---|
| Benchmark sulla scheda (`TEST_MODE 2`) | prime 500 del **test set** MNIST | no | nessuno (model_2026-10: calibrazione con 300 immagini del *training* set). Nel modello di giugno le prime 200 erano anche il set di calibrazione | accuratezza del modello int8 su dati mai visti |
| Test Card con camera (set A, B, C) | immagini 0–236 del **training set** MNIST | no: rientrano nelle prime 6 000 riservate a *validation* (`x_train[:6000]`) | monitoraggio dell'addestramento; **regolazione del pre-processing** | generalizzazione *interna* allo stile MNIST; leggermente ottimistico |
| Cifre scritte a mano (tablet, carta) | nuove | no | nessuno | unico test indipendente: da eseguire **una sola volta a parametri congelati** |

Con il modello di giugno il benchmark int8 (*direct*) era 98,5% sulle 200 immagini di calibrazione e 95,7% sulle 300 mai usate. Nel modello attuale (model_2026-10) la calibrazione è stata spostata sul training set proprio per eliminare questa sovrapposizione: le 500 immagini del benchmark sono tutte pulite (float 96,6%, int8 sulla board 96,4%).

## 14. Benchmark sulla scheda e tempi (22/10/2026)

Accuratezza su 500 immagini del test set MNIST, misurata sulla scheda:

| Percorso | Accuratezza | IC 95% |
|---|---|---|
| Rete int8, input come in addestramento | 96,8% | ±1,5% |
| + pre-processing originale | 94,4% | ±2,0% |
| + pre-processing nuovo | 96,4% (96,0% con bordo di 3 px) | ±1,6% |

Tempi misurati con il contatore di cicli del Cortex-M55 (800 MHz):

| Fase | Tempo | Quota del ciclo con camera |
|---|---|---|
| Inferenza NPU | **118–120 µs** (≈ 23,7 GMAC/s) | 0,2% |
| Pre-processing (modo 1) | 162–176 µs | 0,3% |
| Acquisizione camera | 60,3 ms | 91% |
| Display | 3,8 ms | 5,8% |
| UART | 1,6 ms | 2,4% |
| **Totale** | **66 ms → 15,1 fps** | 100% |

Il sistema è limitato dalla camera: il sensore produce 30 fps, ma il firmware richiede un'istantanea solo a elaborazione conclusa e attende così circa due periodi del sensore. La rete neurale occupa lo 0,2% del tempo.

## 15. Il caso del 9: analisi di un errore sistematico

Con la camera il 9 viene letto come 8 su tutti e tre i set (A, B, C), mentre senza camera il modello riconosce correttamente 52 nove su 54. L'errore è quindi introdotto dal percorso ottico e dal pre-processing, non dalla rete.

Ipotesi considerate, con la relativa obiezione:

1. **Residui di vignettatura negli angoli** (figura §12, pannello 3): il segno in basso a sinistra potrebbe essere interpretato come parte del cerchio inferiore di un 8. *Obiezione:* gli stessi residui compaiono anche in cifre riconosciute correttamente. Test: azzerare una cornice di 3 pixel (`PREPROC_BORDER 3`), come nelle cifre MNIST. Primo controllo superato: sul benchmark il bordo non peggiora le cifre pulite (96,4% → 96,0%, entro l'incertezza).
2. **Tratto troppo spesso e saturo**: il pre-processing porta a bianco pieno tutto ciò che supera il 70% del residuo massimo (`PREPROC_STRETCH 0,7`); un 9 "gonfio", con occhiello largo, si avvicina a un 8. Test previsto se l'ipotesi 1 non basta.

Metodo: per ogni ipotesi la previsione è stata scritta prima del test (il 9 deve diventare corretto senza peggiorare le altre cifre), e i parametri vengono congelati prima dei test indipendenti su scrittura a mano.

## File di riferimento

- `Application/Src/main.c` — ciclo principale, pre-processing (`PREPROC_MODE`), dump di debug (`DEBUG_DUMP_MS`).
- `Doc/test-results.md` — tutti i risultati, inclusi quelli di maggio.
- `Doc/figures/` — figure del 19/10.
- `Doc/logs/` — log UART grezzi delle prove.
- `README.md` (nella cartella del progetto) — mappa delle cartelle e guida rapida.
- `Application/Inc/mnist_test.h` — 500 immagini del test set MNIST per il benchmark sulla scheda.
- Repository di partenza ST: `git remote upstream` → STM32N6-GettingStarted-ImageClassification v2.3.0.
- `flash.ps1` — programmazione della scheda.
