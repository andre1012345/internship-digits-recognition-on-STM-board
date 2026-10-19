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
