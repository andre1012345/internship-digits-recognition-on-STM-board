# Piano di misura per la tesi — STM32N6570-DK

*Creato il 22/10/2026, con la board disponibile a casa per un periodo limitato. Obiettivo: raccogliere tutte le metriche per analisi e conclusioni prima di restituirla.*

## Principio: misurare la perdita anello per anello

| Anello | Cosa si misura | Come | Board? |
|---|---|---|---|
| A. Modello float | accuratezza su MNIST test (10 000 img) | notebook Colab | no |
| B. Modello int8 sulla board, input ideale | perdita di **quantizzazione + deploy** (B − A) | `TEST_MODE 2`, pipeline *direct*, 500 img test MNIST | **sì** |
| C. + pre-processing della board | perdita del **pre-processing** (C − B), modo 0 e modo 1 | `TEST_MODE 2`, pipeline *preproc0* / *preproc1* | **sì** |
| D. + camera e ottica (cifre MNIST su monitor) | perdita di **ottica/inquadratura** (D − C) | pagina MNIST Test Card, set A/B/C | **sì** |
| E. + cifre scritte a mano su carta | perdita da **stile di scrittura reale** (E − D) | protocollo carta (sotto) | **sì** |

## Checklist

### Con la board (priorità)

- [x] **1. Benchmark MNIST automatico** (22/10): B = **96,8%**, C0 = **94,4%**, C1 = **96,4%** su 500 immagini del test set; NPU **118 µs**/inferenza (≈ 23,7 GMAC/s); pre-processing 115 µs (modo 0) / 162 µs (modo 1). Dettagli in `Doc/test-results.md`.
- [x] **2. Tempi per fase con la camera** (22/10): 66,0 ms/frame → **15,1 fps**; acquisizione camera 60,3 ms (91%), display 3,8 ms, UART 1,6 ms, pre-processing 176 µs, **NPU 120 µs (0,2%)**. Sistema limitato dalla camera, non dalla rete.
- [~] **3. Camera + monitor, set B e C** (modo 1): **set B fatto** (22/10) → 9/10 cifre, 87% dei frame; il **9 è letto come 8 anche nel set B**, mentre senza camera il 9 è corretto (52/54) → errore introdotto dal percorso ottico. Manca il set C.
- [ ] **4. A/B controllato modo 0 vs modo 1** stessa inquadratura, stesso set.
- [ ] **5. Carta scritta a mano**: 10 cifre × 3 scritture diverse (pennarello nero, foglio bianco), 10 s ciascuna.
- [ ] **6. Robustezza** (modo 1, set A): distanza 8 / 12 / 18 / 25 cm; luce forte / normale / debole; rotazione ±15°.
- [ ] 7. Foto della board in funzione e del banco di prova (per la tesi).

### Senza board

- [x] Occupazione di memoria (dalla build del 19/10, vedi sotto).
- [x] Complessità del modello: ~2,81 M MAC per inferenza (calcolata dall'architettura).
- [ ] Accuratezza float (anello A): rieseguire `mnist_cnn_augmented.ipynb` su Colab — gli output del notebook non sono stati salvati e i file ONNX non sono più disponibili.
- [ ] Energia: misura diretta non praticabile (corrente MCU solo dal connettore CN2 dissaldando resistori, manuale UM3300 §7.4.6). Usare valori dichiarati da ST come **stima**, oppure misuratore USB sul consumo totale della board.
- [ ] Applicazioni reali e limiti (discussione).

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

### Catena delle perdite (aggiornata al 22/10/2026)

| Anello | Accuratezza | Perdita rispetto all'anello precedente |
|---|---|---|
| A. Float sul PC | *da misurare (Colab)* | — |
| B. Int8 sulla board, input ideale | 96,8% (500 img test) | quantizzazione + deploy: *A − 96,8* |
| C. + pre-processing nuovo (modo 1) | 96,4% | −0,4 punti (entro l'incertezza) |
| C'. + pre-processing originale (modo 0) | 94,4% | −2,4 punti |
| D. + camera e ottica (monitor, set A e B, 20 cifre) | 89% (A) · 87% (B) dei frame | ≈ −8 punti, quasi tutti dovuti al 9 → 8 (sistematico) |
| E. + carta scritta a mano | *da misurare* | — |

### Accuratezza con camera (19/10/2026)

Vedi `Doc/test-results.md`: 40% → 75% → 82% → 89% di frame corretti (cifre MNIST su monitor, set A).

## Protocollo carta (punto 5)

- 3 fogli, uno per "scrittura" (tu, un'altra persona, stampatello volutamente diverso), cifre 0–9, ~6 cm di altezza, tratto ~4 mm.
- Ogni cifra 10 s, poi 2 s di foglio bianco; dire a Claude la cifra prima di mostrarla, oppure seguire l'ordine 0→9.
- Annotare: luce, distanza, chi ha scritto.
