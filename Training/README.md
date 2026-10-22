# Training — dal modello Keras al codice per la NPU

Catena del modello usato dal firmware (model_2026-10). Ogni passo produce file salvati qui.

| Passo | Strumento | Input → Output |
|---|---|---|
| 1. Training | Google Colab, `mnist_cnn_retrain_2026-10.ipynb` (seed 42) | MNIST → `model_2026-10/mnist_cnn_32x32.keras`, `.onnx` (float), `mnist_calib.npz`, `float_metrics.json`, `training_curves.png` |
| 2. Quantizzazione int8 per-canale | ST Edge AI Developer Cloud (Quantize, calibrazione con `mnist_calib.npz`) | `.onnx` float → `model_2026-10/mnist_cnn_32x32_PerChannel_quant_mnist_calib_npz_2.onnx` |
| 3. Compilazione per la NPU | ST Edge AI Core 4.0 locale, `stedgeai/generate_model.bat` | ONNX quantizzato → `stedgeai/out/` (codice C + pesi + `network_generate_report.txt`) |
| 4. Integrazione | copia manuale | `stedgeai/out/*` → `../Model/` (`.raw` rinominato `network_atonbuf.xSPI2.bin`) |

Perché il passo 3 è in locale: il runtime NPU nel progetto (`Middlewares/stedgeai-lib`) è la versione 4.0
(ll_aton 1.1.3) e il codice generato deve corrispondere esattamente; la Developer Cloud usa ormai la 4.1.
`stedgeai/neuralart.json` riproduce le opzioni con cui la Developer Cloud aveva generato il modello di giugno.

Data augmentation usata nel training (identica al notebook di giugno): rotazione ±15°, traslazione ±10 %,
zoom ±15 %, shear 10°. Nessuna variazione dello spessore del tratto.

## Altri file
- `mnist_cnn_augmented.ipynb` — notebook originale di giugno 2026 (modello archiviato in `../Model/archive_2026-06/`).
- `stedgeai/ws/` — cartella temporanea di STEdgeAI, rigenerata dallo script, esclusa da git.
