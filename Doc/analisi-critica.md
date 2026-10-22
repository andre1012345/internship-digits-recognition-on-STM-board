# Analisi critica dei dati — obiezioni prevedibili e limiti

*22/10/2026. Scopo: sapere in anticipo dove il lavoro è attaccabile, cosa si può ancora migliorare (solo finché la board è
disponibile) e cosa va semplicemente dichiarato come limite in tesi.*

## 1. Dati solidi

| Dato | Perché regge |
|---|---|
| Catena float → int8 sulla board (96,6 % → 96,4 %, 500 immagini) | stesse immagini, confronto **immagine per immagine**: float e board danno la stessa risposta su **497/500**; 16 errori in comune, 1 solo del float (idx 324), 2 solo della board (idx 41, 445) |
| Benchmark "pulito" | le 500 immagini sono del test set e la calibrazione della quantizzazione usa il training set: nessuna sovrapposizione |
| Tempo NPU 118 µs | contatore di cicli DWT, 1 500 misure, min/max entro ±5 % |
| Sistema limitato dalla camera (NPU = 0,2 % del frame) | misura per fase, stabile su 5 finestre da 100 frame |
| Riproducibilità del modello | notebook con seed e output, file salvati, script di generazione con la stessa versione del runtime |

## 2. Obiezioni prevedibili e risposta

| Obiezione | Risposta / come presentarlo |
|---|---|
| "96,8 % su MNIST è basso: una CNN arriva al 99 %." | Modello volutamente piccolo (28 k parametri, GlobalAvgPool), input 32×32×3 ricampionato, augmentation pesante pensata per la camera. L'obiettivo era la catena di deploy, non il record su MNIST. È migliorabile (Limiti/sviluppi). |
| "500 immagini sono poche." | Incertezza ≈ ±1,7 punti (95 %). Le differenze tra pipeline (direct / modo 0 / modo 1) e tra modello di giugno e di ottobre sono dello stesso ordine: vanno presentate come **non significative**, salvo il confronto immagine per immagine float/int8. |
| "Frame accuracy 97,5 %: ma i frame non sono indipendenti." | Corretto: ~135 frame della stessa cifra sono **una** osservazione. Il dato statistico è "10 cifre su 10" (n = 10 per set), la percentuale di frame misura solo la stabilità. |
| "Il pre-processing è stato regolato guardando i set A/B/C." | Dichiarato: i set della Test Card sono dati di *tuning*; il bordo di 3 px è stato introdotto dopo averli visti. L'unico test indipendente è il foglio scritto a mano, fatto una volta a parametri congelati. |
| "Come sapete quale cifra c'era in ogni frame?" | Test Card: cadenza fissa (10 s + 2 s). Foglio: ordine 0→9 dichiarato, blocchi separati da frame incerti. È una **ricostruzione a posteriori**: va detto. |
| "Il foglio: una persona, una sessione, un esemplare per cifra." | Sì: indicazioni qualitative (tendenze), non statistiche. I criteri ✅/⚠️/❌ sono descrittivi. |
| "Il modello di giugno non è riproducibile." | Per questo è stato riaddestrato e tutta la catena è stata rifatta: il modello di giugno resta solo come storico. |
| "Perché la quantizzazione sul cloud e la compilazione in locale?" | Il runtime NPU nel progetto è la versione 4.0; il cloud è passato alla 4.1. Generare con la stessa versione del runtime evita incompatibilità (il controllo di versione nel codice generato è esatto: 1.1.3-262). |
| "Uso di strumenti di AI." | Da dichiarare secondo le regole del corso. |

## 3. Dati di cui diffidare

1. **Percentuali di frame con la camera**: dipendono dalla finestra temporale scelta. Nel set A del modello nuovo le finestre
   sono state allineate al primo frame riconosciuto con sicurezza di ogni cifra: l'allineamento usa le predizioni stesse,
   quindi la percentuale è **leggermente ottimistica**. Il dato robusto è "10/10 cifre".
2. **Test su carta n. 2 (rosso sottile)**: assegnazione dei blocchi tra 6 e 8 incerta; non usarlo per conclusioni.
3. **Confronti tra modo 0 e modo 1** e tra modello di giugno e di ottobre sul benchmark: differenze dentro il rumore.
4. **Risultati con camera di maggio e del 19/10**: firmware e inquadratura diversi, nessun log completo per i più vecchi.
   Valgono solo come storia (40 % → 100 %), non come misura.
5. **Accuratezza float del modello di giugno (97,27 %)**: viene da un notebook ricostruito, senza certezza che corrisponda
   ai pesi effettivamente caricati sulla board. Non usarla.

## 4. Limiti

**Non più migliorabili** (vanno solo dichiarati):
- il pre-processing è stato messo a punto sugli stessi set A/B/C usati per valutarlo;
- i fogli scritti a mano sono stati visti: modificare ora modello o parametri e ritestare sugli stessi fogli non
  sarebbe più un test indipendente (servirebbero fogli nuovi);
- la provenienza del modello di giugno.

**Migliorabili solo finché si ha la board:**
- foglio con un 1 corto scritto con buon contrasto (per separare l'ipotesi "trattino" da "contrasto");
- più scrittori / più fogli / più sessioni;
- consumo con un misuratore USB (consumo dell'intera board, non del solo MCU).

**Migliorabili anche senza board** (solo come sviluppi futuri, perché richiederebbero di rifare i test):
- accuratezza del modello (rete più grande, augmentation sullo spessore del tratto);
- classe "nessuna cifra" / rilevatore di fuori-distribuzione;
- pipeline camera a ~30 fps.

## 5. Il flusso di lavoro rispetto agli strumenti ufficiali ST

| Passo | Strumento ST | Note |
|---|---|---|
| Base applicativa | repository ufficiale ST *STM32N6 Getting Started – Image Classification* (GitHub) | modificata e documentata (§11 di `digit-recognition-n6.md`) |
| Quantizzazione int8 | ST Edge AI Developer Cloud | — |
| Compilazione per la NPU | ST Edge AI Core 4.0 (`stedgeai generate --target stm32n6`) | opzioni identiche a quelle usate dal cloud per il modello di giugno, memoria NPU di default di ST |
| Build, firma, flash | STM32CubeIDE 2.1.1, STM32 Signing Tool, STM32CubeProgrammer 2.22 | `flash.ps1` chiama solo questi strumenti, nell'ordine indicato da ST (FSBL, applicazione firmata, pesi) |

Scostamenti da dichiarare: i file generati sono copiati a mano in `Model/` (invece degli script del Model Zoo ST);
i pesi sono scritti come binario grezzo all'indirizzo 0x71000000 (equivalente al file .hex della guida ST);
il pre-processing è fatto dalla CPU, mentre l'esempio ST usa solo l'hardware DCMIPP.
Un punto a favore: nel modello di giugno il controllo di versione del codice generato era stato rilassato a mano;
il modello attuale corrisponde esattamente al runtime e il file generato non è stato modificato.
