# Test Results — STM32N6570-DK MNIST

**Date:** 2026-05-26  
**Model:** `mnist_cnn_32x32_OE_3_3_1`  
**Board:** STM32N6570-DK, IMX335 camera  
**Preprocessing:** RGB888 → inverted grayscale → adaptive midpoint background suppression → [0,1]

---

## TEST_MODE Results (synthetic images)

Tested with `TEST_MODE=1`: 10 pre-rendered 32×32 synthetic images cycling through digits 0–9.  
Each digit was held for 3 s (~75 frames per digit), results were 100% stable per digit.

### Run 1 — original digits.h

| Digit | Predicted | Confidence | Result | Note |
|-------|-----------|------------|--------|------|
| 0 | 0 | 100% | ✅ | |
| 1 | 6 | 87%  | ❌ | Synthetic stroke off-center |
| 2 | 2 | 99%  | ✅ | |
| 3 | 3 | 100% | ✅ | |
| 4 | 8 | 62%  | ❌ | 7-segment style |
| 5 | 5 | 100% | ✅ | |
| 6 | 6 | 100% | ✅ | |
| 7 | 2 | 99%  | ❌ | 7-segment style |
| 8 | 0 | 96%  | ❌ | 7-segment style |
| 9 | 3 | 96%  | ❌ | 7-segment style |

**Run 1 accuracy: 5/10 (50%)**

### Run 2 — redesigned digits.h (1, 4, 7, 8, 9)

| Digit | Predicted | Confidence | Result | Note |
|-------|-----------|------------|--------|------|
| 0 | 0 | 100% | ✅ | |
| 1 | 6 | 78%  | ❌ | Thin stroke still ambiguous |
| 2 | 2 | 99%  | ✅ | |
| 3 | 3 | 100% | ✅ | |
| 4 | 4 | 42%  | ✅ | Fixed by asymmetric redesign |
| 5 | 5 | 100% | ✅ | |
| 6 | 6 | 100% | ✅ | |
| 7 | 2 | 100% | ❌ | Still fails — diagonal not captured at 32×32 |
| 8 | 0 | 89%  | ❌ | Still fails — rectangular loops ≠ MNIST |
| 9 | 2 | 84%  | ❌ | Still fails |

**Run 2 accuracy: 6/10 (60%)**

### Why some synthetic digits fail

The synthetic images use a **7-segment display style** (rectangular bars and vertical strokes).  
The model was trained on **MNIST** (smooth, handwritten curves). For digits with loops or diagonals (7, 8, 9), the rectangular pixel-art shapes don't activate the correct feature detectors in the CNN.

This is a limitation of the synthetic test images, **not of the model itself** — the live camera and screen tests show much higher accuracy.

---

## Live Camera Results — Handwritten digits (paper)

Tested with `TEST_MODE=0` and real handwritten digits on white paper, black marker, ~10–15 cm from camera.

### Early test (digits 1, 2, 3, 5 — after background fix)

| Digit | Confidence (stable) | Result | Notes |
|-------|---------------------|--------|-------|
| 1 | 96–99% | ✅ | Well-centered |
| 2 | 87–97% | ✅ | Stable |
| 3 | 100%   | ✅ | After background fix |
| 5 | 98–99% | ✅ | Most reliable |

### Full handwritten test (digits 1–8, multiple passes)

| Digit | Stable prediction | Confidence | Result | Notes |
|-------|-------------------|------------|--------|-------|
| 1 | 1 | 83–98% | ✅ | Stable across multiple passes |
| 2 | 2 | 88–100% | ✅ | Very reliable |
| 3 | 3 | 63–76% | ⚠️ | Marginal — near or below 70% threshold |
| 4 | 4 | 84–100% | ✅ | Most confident digit in the whole test |
| 5 | 5 | 65–99% | ✅ | Good |
| 6 | 6 | 70–98% | ✅ | Good in well-framed passes |
| 7 | — | <35% | ❌ | Not recognised from handwriting |
| 8 | 8 | 54–87% | ⚠️ | Low confidence, unstable |

**Full handwritten accuracy: 5/8 strong ✅, 2/8 weak ⚠️, 1/8 not detected (7)**

> Digit 7 recognised at 89–96% from PC screen but fails completely from handwriting.  
> Likely cause: handwritten 7 stroke style (angle, serif) diverges from MNIST training distribution.  
> Digit 3 is the weakest reliable digit — thicker strokes and good centering help.

---

## Live Camera Results — PC screen (digits 1–9)

Tested with `TEST_MODE=0`, showing digits 1–9 from a PC monitor ~10–15 cm from camera.  
Note: screen fonts differ from MNIST handwriting — lower contrast and different stroke style.

| Digit | Stable prediction | Confidence | Result | Notes |
|-------|-------------------|------------|--------|-------|
| 0 | 0 | 96–100% | ✅ | Very stable |
| 1 | 1 | 50–72%  | ⚠️ | Low confidence; sometimes confused with 7 |
| 2 | 2 | 89–96%  | ✅ | Stable |
| 3 | 3 | 97–100% | ✅ | Very stable |
| 4 | 4 | 86–98%  | ✅ | Stable |
| 5 | 5 | 81–88%  | ✅ | Stable |
| 6 | — | —       | ❌ | Not reliably detected from screen (single frame at 68%) |
| 7 | 7 | 89–96%  | ✅ | Very stable |
| 8 | 8 | 52–65%  | ⚠️ | Correct but low confidence; needs better framing |
| 9 | 9 | 79–93%  | ✅ | Stable |

**Screen accuracy: 7/9 reliable ✅, 2/9 weak ⚠️, 1/9 not detected (6)**

> Digits 1 and 8 benefit from handwritten input rather than screen fonts.  
> Digit 6 was not reliably recognised from the screen — likely because sans-serif "6" looks different from MNIST handwritten 6.

---

## Preprocessing Fix (background suppression)

**Problem:** Simple min-max contrast stretch amplified ambient lighting gradients in the background, pushing white paper pixels to ~0.3–0.46 after normalization. The model saw false structure (e.g. a "0" ring) even with no digit present.

**Fix:** Adaptive midpoint threshold applied in `main.c`:

```c
/* Pass 2: zero background, stretch digit region to [0,1] */
float midpt    = (vmin + vmax) * 0.5f;
float hi_range = vmax - midpt;
if (hi_range < 0.005f) hi_range = 0.005f;
for (int i = 0; i < n; i++) {
    float raw = dst[3*i];
    float v = (raw <= midpt) ? 0.0f : (raw - midpt) / hi_range;
    dst[3*i] = dst[3*i+1] = dst[3*i+2] = (v > 1.0f) ? 1.0f : v;
}
```

| Digit | Before fix | After fix |
|-------|-----------|-----------|
| 2 (stable) | 0 at 97% (wrong) | 2 at 90–97% ✅ |
| 3 (stable) | 0/8/3 confused | 3 at 100% ✅ |
| 1 (stable) | not tested | 1 at 96–99% ✅ |
| 5 (stable) | not tested | 5 at 98–99% ✅ |

---

## Typical `vmin`/`vmax` Values (live camera)

| Scene | vmin | vmax | range |
|-------|------|------|-------|
| Digit on white paper | 0.33–0.45 | 0.87–0.90 | 0.45–0.57 |
| Empty scene (no digit) | ~0.40 | ~0.42 | ~0.02 (LOW CONTRAST) |

`vmin` ~0.33–0.45 is expected: the IMX335 auto-exposure renders white paper as medium-gray, not 255.

---

## Camera Setup

- Distance: 10–15 cm from paper / screen
- Lighting: indoor ambient (ceiling LED)
- Digit on paper: black marker on A4 white paper, ~5–7 cm tall
- Orientation: digit upright, roughly centered in frame


---

## Re-test 2026-10-19 — int8 model, MNIST digits shown on PC screen

**Firmware:** GitHub "final version" (clean rebuild, `TEST_MODE=0`), model `mnist_cnn_32x321_PerChannel_quant_mnist_calib (1)_npz_1_OE_3_3_1` (int8 in/out, 27.6 KB weights)
**Board:** STM32N6570-DK, IMX335 camera, boot from flash (BOOT0 and BOOT1 both to the left)
**Target:** real MNIST handwritten digits (one sample per class), black on white, shown full-screen on a PC monitor by an auto-advancing test page: 5 s lead-in, then each digit 10 s + 2 s blank
**Camera distance:** ~10–15 cm from the monitor
**Frame rate:** ~15 inferences/s; NPU inference time 0–1 ms per frame
**Analysis:** UART log timestamped on the host; per-digit window = central 8 s of each 10 s slot (~110–125 frames per digit). Slot alignment reconstructed from the fixed 12 s cadence.

| Digit shown | Majority prediction | Frames correct | Mean confidence | Result |
|-------------|--------------------|----------------|-----------------|--------|
| 0 | 2 | 0% | 93% | ❌ |
| 1 | 2 (also 5) | 0% | 96% | ❌ |
| 2 | 2 | 100% | 93% | ✅ |
| 3 | 3 | 100% | 100% | ✅ |
| 4 | 4 | 100% | 92% | ✅ |
| 5 | 2 | 0% | 62% | ❌ |
| 6 | 8 | 2% | 64% | ❌ |
| 7 | 2 | 0% | 77% | ❌ |
| 8 | 8 | 100% | 90% | ✅ |
| 9 | 3 / 8 | 0% | 54% | ❌ |

**Result: 4/10 digits, 40% frame-level accuracy.**

**Observations**
- Correct digits are recognised in a stable way (100% of frames, 90–100% confidence): the model, int8 quantisation and NPU deployment work.
- Errors are systematic, not random: 0, 1, 5 and 7 collapse onto class "2". This points to a consistent distortion of the input (framing/scale, threshold preprocessing, screen pixel pattern) — i.e. a domain gap between camera input and MNIST — rather than to model capacity.
- Next step: dump the 32×32 network input over UART (`DEBUG_DUMP_MS` in `main.c`) and compare it with MNIST samples.

**Setup notes learned on this date**
- Boot from flash = BOOT0 and BOOT1 both to the left. DEV mode (for flashing / debugging) = BOOT1 to the right. Power-cycle after changing switches.
- `CMW_CAMERA_Init` failed with `-7` (no sensor detected) with the morning build; after restoring the sources from git, rebuilding and reseating the camera cable it worked. Exact root cause not isolated.


### Re-test 2026-10-19 (runs 2 and 3) — effect of the preprocessing

Same setup as above (MNIST set A on PC screen, 10 s per digit, ~15 inferences/s, ~110 frames per digit).
Run 2 used the original preprocessing (`PREPROC_MODE 0`, global midpoint threshold) with better camera placement than run 1.
Run 3 used the new preprocessing (`PREPROC_MODE 1`, local background subtraction, 13×13 window).

| Digit | Run 2 — mode 0 (pred, frames correct, conf) | Run 3 — mode 1 (pred, frames correct, conf) |
|-------|---------------------------------------------|---------------------------------------------|
| 0 | 0 — 96% — 53% | 0 — 85% — 98% |
| 1 | 1 — 100% — 87% | 1 — 100% — 100% |
| 2 | 2 — 100% — 97% | 2 — 95% — 97% |
| 3 | 3 — 100% — 98% | 3 — 100% — 100% |
| 4 | 4 — 100% — 95% | 4 — 100% — 100% |
| 5 | **2** — 0% | **5 — 100% — 98%** |
| 6 | 6 — 100% — 88% | 6 — 98% — 92% |
| 7 | 7/2 — 61% — 60% | **3** — 0% (camera framed the monitor edge, see below) |
| 8 | 8 — 100% — 98% | 8 — 100% — 98% |
| 9 | **8** — 0% | 9/4 — 52% — 69% |
| **Frame accuracy** | **75%** | **82%** |

**Findings**
- Root cause of the systematic errors in mode 0: **lens vignetting**. The camera image is darker at the corners; after inversion the corners look like ink and the global threshold keeps them as grey blobs (figure `figures/2026-10-19_board_vs_mnist.png`). These blobs turned 5 into 2 (fake bottom bar) and 9 into 8 (fake lower loop).
- Mode 1 subtracts a local background estimate, which removes the vignetting (figure `figures/2026-10-19_preproc_old_vs_new.png`): 5 is fixed (0% → 100%), 9 partially (0% → 52%), mean confidence rises to 92–100% on most digits.
- Blank screen (between digits): mode 1 gives low confidence (~15–20%) on a flat scene; 23% of transition frames still exceed 70% (motion between digits).
- Run 3 digit 7: the debug dump shows the top edge of the monitor inside the frame, producing a horizontal bar above the digit → predicted 3. This is a framing issue, not a model issue; it should be re-tested with the target fully inside the frame.
- Run 1 vs run 2 (same firmware, mode 0): 40% vs 75% → the original preprocessing is very sensitive to camera placement.


### Re-test 2026-10-19 (run 4) — new preprocessing, corrected framing

Same firmware as run 3 (`PREPROC_MODE 1`). The camera was fixed in place with only the white page visible around the digit (no monitor edge). MNIST set A, 10 s per digit, ~110 frames per digit. Log: `logs/2026-10-19_test4.txt`.

| Digit | Prediction | Frames correct | Mean confidence |
|-------|-----------|----------------|-----------------|
| 0 | 0 | 100% | 98% |
| 1 | 1 | 100% | 100% |
| 2 | 2 | 100% | 99% |
| 3 | 3 | 100% | 100% |
| 4 | 4 | 100% | 100% |
| 5 | 5 | 100% | 93% |
| 6 | 6 | 100% | 99% |
| 7 | 7 | 100% | 92% |
| 8 | 8 | 100% | 96% |
| 9 | **8** (76%), 3 (19%) | 0% | — |

**Result: 9/10 digits, 89% frame accuracy** (run 1: 40%, run 2: 75%, run 3: 82%).

- With correct framing the 7 is recognised on every frame (run 3 failure confirmed as a framing issue).
- Blank screen between digits: only 6 of 170 transition frames (4%) exceed 70% confidence.
- The set-A sample of digit 9 is classified as 8 in runs 2, 3 and 4; the debug dump shows a clean, well-centred 9. Next step: test sets B and C to tell whether this is specific to this sample or a general 9/8 confusion of the model.


---

## Benchmark 2026-10-22 — MNIST test set on the board, no camera (`TEST_MODE 2`)

**Purpose:** separate the accuracy loss of each stage of the chain, without camera/optics.
**Data:** first 500 images of the official MNIST test set (t10k), 28→32 bilinear resize as in training (`Application/Inc/mnist_test.h`).
**Pipelines:** every image goes through three paths on the board:
- **direct** — fed to the int8 network exactly as in training → measures quantisation + deployment;
- **preproc0** — rendered as dark ink on white (ideal camera, no optics) and passed through the original preprocessing (`PREPROC_MODE 0`);
- **preproc1** — same, through the new preprocessing (`PREPROC_MODE 1`).

**Board:** STM32N6570-DK, CPU 800 MHz, Neural-ART NPU. Log: `logs/2026-10-22_bench_mnist.txt`.

| Pipeline | Accuracy (500 img) | 95% CI | Top-1 confidence < 70% | Errors not present in *direct* |
|---|---|---|---|---|
| direct (int8 on board) | **96.8%** (484/500) | ±1.5% | 18 | — |
| preproc0 (original) | **94.4%** (472/500) | ±2.0% | 54 | 16 |
| preproc1 (new) | **96.4%** (482/500) | ±1.6% | 19 | 4 (and 2 direct errors fixed) |

**Timing (DWT cycle counter, 1 500 inferences):**

| Stage | Mean | Min | Max |
|---|---|---|---|
| NPU inference (`stai_network_run`) | **118.2 µs** | 118.0 µs | 126.5 µs |
| Preprocessing mode 0 | 114.5 µs | 107.7 µs | 124.6 µs |
| Preprocessing mode 1 | 162.2 µs | 155.9 µs | 167.7 µs |

→ Effective NPU throughput ≈ 2.81 M MAC / 118 µs ≈ **23.7 GMAC/s**; ~8 460 inferences/s if the NPU ran alone. The whole 1 500-inference benchmark took 639 ms.

**Findings**
- The int8 network on the board reaches **96.8%** on real MNIST test digits: quantisation and deployment work correctly. The float reference on the same images is still to be measured (Colab) to quantify the quantisation loss exactly.
- The **original preprocessing costs 2.4 points** even on ideal, vignetting-free images (16 new errors, 3× more low-confidence predictions): the global threshold thins or breaks strokes.
- The **new preprocessing is practically lossless** (−0.4 points, within the confidence interval) at +48 µs per frame.
- Most frequent confusions (direct): 9↔8, 5→3, 4→{2,6,8,9}, 6→0 — typical MNIST confusions, not deployment artefacts.
- Measured inference time (118 µs) is ~40× below the "< 5 ms" previously estimated.


---

## Camera run 2026-10-22 — MNIST set B on monitor + per-stage timing

**Firmware:** `TEST_MODE 0`, `PREPROC_MODE 1`, `DEBUG_DUMP_MS 0`, `BENCH_PERIOD_FRAMES 100`. Board in DEV mode, run from the debugger.
**Target:** MNIST Test Card, **set B** (different handwriting samples from set A), 10 s per digit + 2 s blank, camera fixed at 10–15 cm.
**Log:** `logs/2026-10-22_setB_timing.txt` (2 227 frames). Slots reconstructed from the constant frame rate (15.14 fps) and the 12 s cadence.

### Accuracy, set B

| Digit | Prediction | Frames correct | Mean confidence |
|---|---|---|---|
| 0 | 0 | 100% | 92% |
| 1 | 1 | 100% | 100% |
| 2 | 2 | 100% | 94% |
| 3 | 3 | 100% | 100% |
| 4 | 4 | 100% | 100% |
| 5 | 5 (3 on 17%) | 82% | 80% |
| 6 | 6 | 100% | 97% |
| 7 | 7 | 100% | 94% |
| 8 | 8 | 96% | 95% |
| 9 | **8** | 0% | — |

**Result: 9/10 digits, 87% frame accuracy** (set A, run 4: 9/10, 89%).

- The **9 is read as 8 on both sets** through the camera, while on the board without camera (benchmark, *preproc1*) 52 of 54 nines are correct. The confusion is therefore introduced by the optical path (blur/stroke thickening closing the lower part of the 9), not by the network or the preprocessing. To be investigated with `DEBUG_DUMP_MS`.
- Blank screen between digits: 29% of the transition frames exceed 70% confidence (4% in run 4) → a "no digit" class or temporal smoothing is needed for a real application.

### Per-stage timing (camera loop, 5 windows × 100 frames, values stable across windows)

| Stage | Mean | Share of the loop |
|---|---|---|
| ISP update | 24–607 µs (peaks 3.1 ms when the auto-exposure updates) | < 1% |
| **Camera capture (wait for the 32×32 snapshot)** | **60.3 ms** | **91%** |
| Preprocessing (mode 1) | 176 µs | 0.3% |
| **NPU inference** | **120 µs** | **0.2%** |
| Post-processing (dequantise + sort) | 1.7 µs | ~0% |
| Display overlay (LCD text) | 3.8 ms (6.1 ms in the first window) | 5.8% |
| UART print (~25 characters @ 115 200 baud, blocking) | 1.57 ms | 2.4% |
| **Total per frame** | **66.0 ms → 15.14 fps** | 100% |

**Findings**
- The system is **camera-bound**: the sensor runs at 30 fps, but the loop starts a single snapshot only after finishing the previous frame, so it always waits ~2 sensor periods (≈ 60 ms). The neural network uses **0.2%** of the frame time.
- Throughput could roughly double (≈ 30 fps) by overlapping capture and processing (continuous/double-buffered NN pipe) — a software change, not a hardware limit.
- Display and UART cost more than the inference itself; in a deployed product they would be reduced (DMA UART, fewer LCD redraws).


---

## Camera run 2026-10-22 — MNIST set C on monitor (debug dump on)

Same setup as the set B run, `DEBUG_DUMP_MS 2000`. Log: `logs/2026-10-22_setC_dump.txt` (PuTTY appended it to the set B log; extracted to its own file).

| Digit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Frames correct | 100% | 100% | 100% | 100% | 100% | 97% | 100% | 100% | 100% | **0% (→ 8)** |
| Mean confidence | 97% | 100% | 93% | 100% | 100% | 65% | 97% | 93% | 98% | — |

**Result: 9/10 digits, 90% frame accuracy.** Over sets A, B and C the camera chain recognises 27 of 30 digit samples; the three failures are all **9 → 8**.

**Hypothesis under test (not yet confirmed):** the debug dump shows a clean 9, but small residual marks in the top-left and bottom-left corners (vignetting residue where the background window is clipped). MNIST digits always have an empty ~4-px border. Fix being tested: `PREPROC_BORDER 3` (zero a 3-px frame in mode 1).
*Objection noted:* the same corner marks appear for digits that are recognised correctly, and other differences (stroke thickness, blur) could explain the 9. Prediction written before the test: if the border is the cause, the 9 must become correct on most frames and the other digits must not get worse; the MNIST benchmark (`TEST_MODE 2`) must stay at ~96.4%. Parameters tuned on sets A/B/C; the handwritten-paper test will be the independent validation.


### Benchmark 2026-10-22 with `PREPROC_BORDER 3` (step 1 of the border hypothesis test)

Same 500 MNIST test images, `TEST_MODE 2`. Log: `logs/2026-10-22_bench_mnist_border.txt` (copied from the PuTTY window; the file log failed because of a malformed log-file name).

| Pipeline | Without border | With 3-px border |
|---|---|---|
| direct | 96.8% | 96.8% (unchanged: the border does not apply here) |
| preproc0 | 94.4% | 94.4% (unchanged: mode 0 only) |
| preproc1 | 96.4% | **96.0%** (−2 images: two 7s read as 3; within the ±1.6% interval) |
| preproc1 time | 162 µs | 169 µs |

→ Condition "clean MNIST digits must not get worse" is met within measurement uncertainty. Step 2 (camera, digit 9) pending.


### Camera run 2026-10-22 — set A with `PREPROC_BORDER 3` (step 2 of the border hypothesis test)

Same setup as run 4 (camera fixed, only white page around the digit), firmware with `PREPROC_MODE 1` + `PREPROC_BORDER 3`, debug dump on. Log: `logs/2026-10-22_setA_border.txt` (1 059 frames analysed; digit 0 partly before the timing capture started).

| Digit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Frames correct | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | **100%** |
| Mean confidence | 100% | 100% | 100% | 100% | 100% | 75% | 100% | 97% | 100% | 85% |

**Result: 10/10 digits, 100% frame accuracy.** Blank screen between digits: 5% of transition frames above 70% confidence.

**Hypothesis confirmed:** the 9 → 8 error (runs 2, 3, 4 on set A; sets B and C) disappears when the 3-px frame is cleared; the debug dump shows clean corners. The prediction written before the test (9 correct, no other digit worse, MNIST benchmark ≈ 96%) is met.

**Caveat:** the border was introduced after looking at sets A/B/C, so this result is on data used for tuning. Parameters are now frozen (`PREPROC_MODE 1`, `BG_RADIUS 6`, `MIN_CONTRAST 0.06`, `STRETCH 0.7`, `BORDER 3`) for the independent tests (tablet and paper handwriting).

Progression on set A: 40% → 75% → 82% → 89% → **100%**.
