# Piano di misura per la tesi — STM32N6570-DK

*Creato il 22/10/2026, con la board disponibile a casa per un periodo limitato. Obiettivo: raccogliere tutte le metriche per analisi e conclusioni prima di restituirla.*

**Modello di riferimento della tesi: model_2026-10** (riaddestrato il 22/10, catena completa e riproducibile, vedi `Training/README.md`). Le misure fatte col modello di giugno (archiviato in `Model/archive_2026-06/`) restano in `Doc/test-results.md` come prove precedenti.

## Principio: misurare la perdita anello per anello

| Anello | Cosa si misura | Come | Board? |
|---|---|---|---|
| A. Modello float | accuratezza su MNIST test | notebook Colab | no |
| B. Modello int8 sulla board, input ideale | perdita di **quantizzazione + deploy** (B − A) | `TEST_MODE 2`, pipeline *direct*, 500 img test MNIST | **sì** |
| C. + pre-processing della board | perdita del **pre-processing** (C − B), modo 0 e modo 1 | `TEST_MODE 2`, pipeline *preproc0* / *preproc1* | **sì** |
| D. + camera e ottica (cifre MNIST su monitor) | perdita di **ottica/inquadratura** (D − C) | pagina MNIST Test Card, set A/B/C | **sì** |
| E. + cifre scritte a mano su carta | perdita da **stile di scrittura reale** (E − D) | foglio davanti alla camera | **sì** |

## Catena delle perdite — model_2026-10 (22/10/2026)

| Anello | Risultato | Perdita rispetto all'anello precedente |
|---|---|---|
| A. Float sul PC | **96,6 %** sulle stesse 500 img (96,81 % su 10 000) | — |
| B. Int8 sulla board, input ideale | **96,4 %** (482/500) | quantizzazione + deploy: **−1 immagine su 500** |
| C. + pre-processing modo 1 (+ bordo) | 94,6 % | −1,8 punti |
| C'. + pre-processing modo 0 (originale ST) | 93,8 % | −2,6 punti |
| D. + camera, monitor (set A) | **10/10 cifre**, 97,5 % dei frame | errori solo nelle transizioni tra cifre |
| E. + carta scritta a mano (blu, primo tentativo) | **7/10 cifre** stabili | domain shift: stile (1 con trattino → 7, ipotesi non confermata), spessore del tratto, forme di 0 e 9 |

## Checklist

### Con la board

- [x] **1. Benchmark MNIST automatico**: giugno e model_2026-10 (sopra). NPU **118 µs**/inferenza.
- [x] **2. Tempi per fase con la camera**: 66,0 ms/frame → **15,1 fps**; camera 91 %, **NPU 0,2 %**. Sistema limitato dalla camera.
- [x] **3. Camera + monitor**: set A, B, C (modello di giugno); bordo 3 px → 10/10 sul set A; set A ripetuto con model_2026-10 → 10/10.
- [x] **5. Carta scritta a mano** (model_2026-10): blu (tratto spesso, inchiostro quasi esaurito) 7/10; rosso sottile con 1 corto ~5/10 (l'1 corto non è riconosciuto); rosso ripassato 7/10 (cifre 2–6 e 8 pulite). Una sola persona e una sessione: tendenze, non statistiche.
- [ ] 4. A/B controllato modo 0 vs modo 1 con la camera (facoltativo: il confronto c'è già nel benchmark).
- [ ] 6. Robustezza (distanza, luce, rotazione) — facoltativo.
- [~] 7. Foto: fogli fatti (`Doc/figures/`); manca una foto della board in funzione sul banco di prova.
- [ ] Pulsante USER per le etichette: non ha scritto nel log, da verificare se serve ancora.

### Senza board

- [x] Occupazione di memoria (sotto).
- [x] Complessità del modello: ~2,81 M MAC per inferenza.
- [x] Accuratezza float (anello A): rieseguito su Colab (`Training/mnist_cnn_retrain_2026-10.ipynb`).
- [ ] Energia: misura diretta non praticabile (corrente MCU solo dal connettore CN2 dissaldando resistori, manuale UM3300 §7.4.6). Usare valori dichiarati da ST come **stima**, oppure misuratore USB sul consumo totale.
- [ ] Applicazioni reali, limiti, sviluppi futuri (augmentation sullo spessore del tratto, classe "nessuna cifra", autoencoder come rilevatore di fuori-distribuzione).

## Dati già raccolti

### Memoria (build Debug, 19/10/2026)

| Sezione | Byte | Dove |
|---|---|---|
| Codice (.text) | 130 848 | AXISRAM1 (copiato dalla flash dal bootloader) |
| Costanti (.rodata) | 138 768 | AXISRAM1 |
| Dati inizializzati + .bss | 948 + 19 256 | AXISRAM1 |
| Heap + stack | 16 896 | AXISRAM1 |
| Buffer immagine/display (.psram) | 2 304 000 | PSRAM esterna |
| Pesi del modello | 28 289 | flash NOR esterna (0x71000000) |

Ripartizione del codice + costanti per componente: camera/ISP 112 KB · applicazione 59 KB (di cui la maggior parte immagini/logo) · HAL/BSP 44 KB · libreria C 28 KB · **runtime rete neurale 27,6 KB** (16,6 KB codice) · display 9 KB.

### Complessità del modello

| Strato | MAC |
|---|---|
| Conv 3×3, 3→16, 32×32 | 442 368 |
| Conv 3×3, 16→32, 16×16 | 1 179 648 |
| Conv 3×3, 32→64, 8×8 | 1 179 648 |
| Dense 64→64 | 4 096 |
| Dense 64→10 | 640 |
| **Totale** | **≈ 2,81 M MAC** |
