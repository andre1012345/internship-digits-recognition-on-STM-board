 /**
 ******************************************************************************
 * @file    main.c
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
#include <string.h>
#include <unistd.h>

#include "cmw_camera.h"
#include "stm32n6570_discovery_bus.h"
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6570_discovery_xspi.h"
#include "stm32n6570_discovery.h"
#include "stm32_lcd.h"
#include "app_fuseprogramming.h"
#include "stm32_lcd_ex.h"
#include "stai.h"
#include "stai_network.h"
#include "app_camerapipeline.h"
#include "main.h"
#include <stdio.h>
#include "app_config.h"
#include "crop_img.h"
#include "stlogo.h"
#include "arm_math.h"

#define TEST_MODE 0  /* 0 = live camera, 1 = synthetic digit cycling */
/* DEBUG_DUMP_MS: every N ms print on UART the 32x32 camera input (left) and
   the preprocessed network input (right) as ASCII art. 0 = disabled. */
#define DEBUG_DUMP_MS 2000

/* PREPROC_MODE: 0 = original global midpoint threshold,
                 1 = local background subtraction (vignetting-robust) */
#define PREPROC_MODE          1
#define PREPROC_BG_RADIUS     6      /* background window = 13x13 px on the 32x32 frame */
#define PREPROC_MIN_CONTRAST  0.06f  /* residuals below this are treated as background */
#define PREPROC_STRETCH       0.7f   /* strokes at >= 70% of max residual map to 1.0 */

/* Copy of the preprocessed network input (0..255) for the debug dump: the real
   input buffer is reused by the NPU as activation memory during inference. */
static uint8_t net_in_dbg[STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT];
#if TEST_MODE
#include "digits.h"
#define DIGIT_HOLD_MS 3000  /* ms to hold each digit before advancing */
#endif

CLASSES_TABLE;

#define LCD_FG_WIDTH  SCREEN_WIDTH
#define LCD_FG_HEIGHT SCREEN_HEIGHT
#define LCD_FG_FRAMEBUFFER_SIZE  (LCD_FG_WIDTH * LCD_FG_HEIGHT * 2)

#ifndef APP_GIT_SHA1_STRING
#define APP_GIT_SHA1_STRING "dev"
#endif
#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "unversioned"
#endif


typedef struct
{
  uint32_t X0;
  uint32_t Y0;
  uint32_t XSize;
  uint32_t YSize;
} Rectangle_TypeDef;

/* Lcd Background area */
Rectangle_TypeDef lcd_bg_area = {
#if ASPECT_RATIO_MODE == ASPECT_RATIO_CROP || ASPECT_RATIO_MODE == ASPECT_RATIO_FIT
  .X0 = (LCD_FG_WIDTH - LCD_FG_HEIGHT) / 2,
#else
  .X0 = 0,
#endif
  .Y0 = 0,
  .XSize = 0,
  .YSize = 0,
};

/* Lcd Foreground area */
Rectangle_TypeDef lcd_fg_area = {
  .X0 = 0,
  .Y0 = 0,
  .XSize = LCD_FG_WIDTH,
  .YSize = LCD_FG_HEIGHT,
};

UART_HandleTypeDef huart1;
volatile int32_t cameraFrameReceived;
stai_ptr nn_in;
BSP_LCD_LayerConfig_t LayerConfig = {0};
void *pp_input;
char const *nn_top1_output_class_name;
float nn_top1_output_class_proba;
int nn_ranking[NB_CLASSES];
static float nn_out_float[NB_CLASSES];

#define ALIGN_TO_16(value) (((value) + 15) & ~15)

/* When NN input dimensions are not a multiple of 16, the DCMIPP output needs cropping */
#if (STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL) != ALIGN_TO_16(STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL)
#define DCMIPP_NN_NEEDS_CROP 1
#define DCMIPP_OUT_NN_LEN (ALIGN_TO_16(STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL) * STAI_NETWORK_IN_1_HEIGHT)
#define DCMIPP_OUT_NN_BUFF_LEN (DCMIPP_OUT_NN_LEN + 32 - DCMIPP_OUT_NN_LEN%32)

__attribute__ ((aligned (32)))
static uint8_t dcmipp_out_nn[DCMIPP_OUT_NN_BUFF_LEN];
#else
#define DCMIPP_NN_NEEDS_CROP 0
#endif

/* Intermediate uint8 capture buffer: DCMIPP always writes uint8 RGB888 here;
   a software step converts it to float32 in nn_in before inference. */
#define NN_RAW_BUF_SIZE (STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT * 3)
__attribute__((aligned(32)))
static uint8_t nn_raw[NN_RAW_BUF_SIZE];

/* model */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)
/* Lcd Background Buffer */
__attribute__ ((section (".psram_bss")))
__attribute__ ((aligned (32)))
static uint8_t lcd_bg_buffer[800 * 480 * 2];
/* Lcd Foreground Buffer */
__attribute__ ((section (".psram_bss")))
__attribute__ ((aligned (32)))
static uint8_t lcd_fg_buffer[2][LCD_FG_WIDTH * LCD_FG_HEIGHT * 2];
static int lcd_fg_buffer_rd_idx;

static void SystemClock_Config(void);
static void CONSOLE_Config(void);
static void NPURam_enable(void);
static void NPUCache_config(void);
static void Display_NetworkOutput(uint32_t inference_ms);
static void Network_Postprocess(void);
static void Bubblesort(float *prob, int *classes, int size);
static void LCD_init(void);
#if DEBUG_DUMP_MS
static void Debug_DumpInput(const uint8_t *raw_rgb, const uint8_t *net_in, int pred, int conf);
#endif
static void Security_Config(void);
static void set_clk_sleep_mode(void);
static void IAC_Config(void);
static void Display_WelcomeScreen(void);
static void Hardware_init(void);
static void NeuralNetwork_init(uint32_t *nn_in_length, stai_ptr *nn_out, stai_size *number_output, int32_t nn_out_len[]);


/**
  * @brief  Main program
  * @param  None
  * @retval None
  */
int main(void)
{
  Hardware_init();

  /*** NN Init ****************************************************************/
  stai_size number_output = 0;
  stai_ptr nn_out[STAI_NETWORK_OUT_NUM] = {0};
  int32_t nn_out_len[STAI_NETWORK_OUT_NUM] = {0};

  {
    uint32_t nn_in_len = 0;
    NeuralNetwork_init(&nn_in_len, nn_out, &number_output, nn_out_len);
  }

  /* Warn if NOR flash is blank — weights at 0xFFFFFFFF produce NaN inference */
  if (*(volatile uint32_t *)0x71000004UL == 0xFFFFFFFFUL)
    printf("[ERROR] NOR flash blank - run: .\\flash.ps1 -Weights\r\n");

  /*** Post Processing Init ***************************************************/
  stai_network_info info;
  int ret;

  ret = stai_network_get_info(network_context, &info);
  assert(ret == STAI_SUCCESS);
  pp_input = nn_out_float;

  /*** Camera + LCD Init ******************************************************/
  uint32_t pitch_nn = 0;
  CameraPipeline_Init(&lcd_bg_area.XSize, &lcd_bg_area.YSize, &pitch_nn);
  LCD_init();
  CameraPipeline_DisplayPipe_Start(lcd_bg_buffer, CMW_MODE_CONTINUOUS);

  /*** Startup banner *********************************************************/
  printf("========================================\r\n");
  printf("MNIST Digit Classification - STM32N6570-DK\r\n");
  printf("Model : %s\r\n", STAI_NETWORK_ORIGIN_MODEL_NAME);
  printf("Build : %s %s\r\n", __DATE__, __TIME__);
  printf("========================================\r\n");

  /*
   * =========================================================================
   *  PIPELINE OVERVIEW
   * =========================================================================
   *
   *  IMX335 camera
   *    │  CSI-2 → DCMIPP (hardware scaler)
   *    ├─ Pipe 1 (continuous): full-res RGB565 → lcd_bg_buffer  (LCD preview)
   *    └─ Pipe 2 (snapshot):   32×32 RGB888  → nn_raw[3072]
   *
   *  nn_raw  uint8[32×32×3]  — raw camera pixels, one frame per inference
   *    │
   *    │  [CPU] Preprocessing — two passes over 1024 pixels
   *    │
   *    │  Pass 1: inverted grayscale
   *    │    gray = 0.299·R + 0.587·G + 0.114·B   (Rec.601 luma)
   *    │    val  = (255 − gray) / 255             (invert: ink→bright, paper→dark)
   *    │    → scan all pixels to find vmin and vmax
   *    │
   *    │  Pass 2: adaptive background suppression
   *    │    midpt    = (vmin + vmax) / 2
   *    │    hi_range = vmax − midpt
   *    │    v = 0.0                      if val ≤ midpt   (background → 0)
   *    │    v = (val − midpt) / hi_range if val > midpt   (digit → [0,1])
   *    │    Replicate v into R,G,B (model expects 3-channel float32 input).
   *    │    Rationale: simple min-max stretch amplifies uneven lighting in the
   *    │    background (vmin≈0.33–0.45 on white paper), pushing background pixels
   *    │    to ~0.3 after stretch and causing the classifier to see false structure
   *    │    (e.g. a "0" ring).  The midpoint threshold forces background to exactly
   *    │    0.0, matching the MNIST training distribution.
   *    │
   *    ▼
   *  nn_in   int8[32×32×3 = 3 072 bytes]  — model input (quantised, scale=1/255, zp=-128)
   *    │
   *    │  [NPU] Neural-ART CNN inference — mnist_cnn_32x32_aug_int8
   *    │        Weights in NOR flash @ 0x71000000  (<5 ms @ 1 GHz, 6/7 EC epochs)
   *    │
   *    ▼
   *  nn_out  int8[10]  — quantised output (scale=1/256, zp=-128)
   *    │
   *    │  [CPU] Dequantise → nn_out_float[10] in [0,1]
   *    │  [CPU] Postprocess: bubblesort → nn_ranking[] (highest prob first)
   *    │         threshold: reject if top-1 < 0.70
   *    │
   *    ▼
   *  LTDC layer 2  — classification results overlaid on camera preview
   *  USART1        — per-frame log at 115200 baud
   *    live mode : "[digit] confidence%  (Xms)"
   *    TEST_MODE : "digit=N  predicted=M  OK|WRONG  (Xms)"
   * =========================================================================
   */

  /*** App Loop ***************************************************************/
  while (1)
  {
    CameraPipeline_IspUpdate();

    /* --- [1] FRAME ACQUISITION --------------------------------------------- */
#if TEST_MODE
    /* Cycle through digits 0-9, advancing every DIGIT_HOLD_MS milliseconds */
    static int     current_digit   = 0;
    static uint32_t digit_start_ms = 0;
    if (digit_start_ms == 0) digit_start_ms = HAL_GetTick();
    if (HAL_GetTick() - digit_start_ms >= DIGIT_HOLD_MS)
    {
      current_digit  = (current_digit + 1) % 10;
      digit_start_ms = HAL_GetTick();
      printf("\r\n>>> Switching to digit %d <<<\r\n", current_digit);
    }
    memcpy(nn_raw, digits_table[current_digit], sizeof(nn_raw));
#else
#if DCMIPP_NN_NEEDS_CROP
    /* Start NN camera single capture Snapshot into intermediate buffer */
    CameraPipeline_NNPipe_Start(dcmipp_out_nn, CMW_MODE_SNAPSHOT);
#else
    /* Start NN camera single capture Snapshot into raw uint8 buffer */
    CameraPipeline_NNPipe_Start(nn_raw, CMW_MODE_SNAPSHOT);
#endif

    while (cameraFrameReceived == 0) {};
    cameraFrameReceived = 0;

#if DCMIPP_NN_NEEDS_CROP
    /*
     * Crop the image: the DCMIPP hardware requires output dimensions to be
     * multiples of 16, so we crop the padded buffer into nn_raw.
     */
    SCB_InvalidateDCache_by_Addr(dcmipp_out_nn, sizeof(dcmipp_out_nn));
    img_crop(dcmipp_out_nn, nn_raw, pitch_nn, STAI_NETWORK_IN_1_WIDTH, STAI_NETWORK_IN_1_HEIGHT, STAI_NETWORK_IN_1_CHANNEL);
#else
    /* Invalidate dcache for the DMA-written nn_raw before CPU reads it */
    SCB_InvalidateDCache_by_Addr(nn_raw, sizeof(nn_raw));
#endif
#endif /* !TEST_MODE */

    uint32_t ts[2] = { 0 };

    /* --- [2] PREPROCESSING: RGB888 uint8 → int8 (scale=1/255, zp=-128) ---
     * Same two-pass algorithm; output written as int8 to match the quantised
     * model input format (STAI_FORMAT_S8) generated by STEdgeAI. */
    {
      const uint8_t *src = nn_raw;
      int8_t        *dst = (int8_t *)nn_in;
      int n = STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT;

#if PREPROC_MODE == 0
      /* --- Mode 0 (original): global midpoint threshold --------------------- */
      /* Pass 1: find scene vmin/vmax */
      float vmin = 1.0f, vmax = 0.0f;
      for (int i = 0; i < n; i++)
      {
        float gray = 0.299f * src[3*i] + 0.587f * src[3*i+1] + 0.114f * src[3*i+2];
        float val  = (255.0f - gray) / 255.0f;
        if (val < vmin) vmin = val;
        if (val > vmax) vmax = val;
      }

      float midpt    = (vmin + vmax) * 0.5f;
      float hi_range = vmax - midpt;
      if (hi_range < 0.005f) hi_range = 0.005f;

      /* Pass 2: suppress background, quantise to int8 */
      for (int i = 0; i < n; i++)
      {
        float gray = 0.299f * src[3*i] + 0.587f * src[3*i+1] + 0.114f * src[3*i+2];
        float val  = (255.0f - gray) / 255.0f;
        float v = (val <= midpt) ? 0.0f : (val - midpt) / hi_range;
        if (v > 1.0f) v = 1.0f;
        net_in_dbg[i] = (uint8_t)(v * 255.0f);
        dst[3*i] = dst[3*i+1] = dst[3*i+2] = (int8_t)((int)(v * 255.0f) - 128);
      }
#else
      /* --- Mode 1: local background subtraction (removes lens vignetting) ----
       * val = inverted luminance (ink -> bright). The background is estimated
       * per pixel as the mean of a (2R+1)x(2R+1) window (integral image), then
       * subtracted: slow shading such as vignetting cancels out, thin strokes
       * remain. Small residuals are zeroed, the rest is stretched to [0,1]. */
      const int W = STAI_NETWORK_IN_1_WIDTH, H = STAI_NETWORK_IN_1_HEIGHT;
      static float val[STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT];
      static float integ[(STAI_NETWORK_IN_1_WIDTH + 1) * (STAI_NETWORK_IN_1_HEIGHT + 1)];

      for (int i = 0; i < n; i++)
      {
        float gray = 0.299f * src[3*i] + 0.587f * src[3*i+1] + 0.114f * src[3*i+2];
        val[i] = (255.0f - gray) / 255.0f;
      }
      for (int x = 0; x <= W; x++) integ[x] = 0.0f;
      for (int y = 1; y <= H; y++)
      {
        float row = 0.0f;
        integ[y * (W + 1)] = 0.0f;
        for (int x = 1; x <= W; x++)
        {
          row += val[(y - 1) * W + (x - 1)];
          integ[y * (W + 1) + x] = integ[(y - 1) * (W + 1) + x] + row;
        }
      }

      float dmax = 0.0f;
      for (int y = 0; y < H; y++)
      {
        int y0 = (y - PREPROC_BG_RADIUS < 0) ? 0 : y - PREPROC_BG_RADIUS;
        int y1 = (y + PREPROC_BG_RADIUS >= H) ? H - 1 : y + PREPROC_BG_RADIUS;
        for (int x = 0; x < W; x++)
        {
          int x0 = (x - PREPROC_BG_RADIUS < 0) ? 0 : x - PREPROC_BG_RADIUS;
          int x1 = (x + PREPROC_BG_RADIUS >= W) ? W - 1 : x + PREPROC_BG_RADIUS;
          float sum = integ[(y1 + 1) * (W + 1) + (x1 + 1)] - integ[y0 * (W + 1) + (x1 + 1)]
                    - integ[(y1 + 1) * (W + 1) + x0]       + integ[y0 * (W + 1) + x0];
          float bg  = sum / (float)((y1 - y0 + 1) * (x1 - x0 + 1));
          float d   = val[y * W + x] - bg;
          if (d < PREPROC_MIN_CONTRAST) d = 0.0f;
          val[y * W + x] = d;               /* reuse buffer: now holds the residual */
          if (d > dmax) dmax = d;
        }
      }

      /* Empty / flat scene: keep the input black so the network sees nothing */
      float scale = (dmax < 0.05f) ? 0.0f : 1.0f / (PREPROC_STRETCH * dmax);
      for (int i = 0; i < n; i++)
      {
        float v = val[i] * scale;
        if (v > 1.0f) v = 1.0f;
        net_in_dbg[i] = (uint8_t)(v * 255.0f);
        dst[3*i] = dst[3*i+1] = dst[3*i+2] = (int8_t)((int)(v * 255.0f) - 128);
      }
#endif
      SCB_CleanInvalidateDCache_by_Addr(nn_in, STAI_NETWORK_IN_1_SIZE_BYTES);
    }

    /* --- [3] NPU INFERENCE ------------------------------------------------- */
    ts[0] = HAL_GetTick();
    ret = stai_network_run(network_context, STAI_MODE_SYNC);
    assert(ret == 0);
    ts[1] = HAL_GetTick();

    /* Invalidate DCache so CPU sees the NPU-written nn_out values */
    for (int i = 0; i < (int)number_output; i++)
      SCB_InvalidateDCache_by_Addr(nn_out[i], nn_out_len[i]);

    /* Dequantise int8 output → float probabilities (scale=1/256, zp=-128) */
    {
      const int8_t *raw_out = (const int8_t *)nn_out[0];
      for (int i = 0; i < NB_CLASSES; i++)
        nn_out_float[i] = ((int)raw_out[i] + 128) * (1.0f / 256.0f);
    }

    /* --- [4] POSTPROCESS + DISPLAY ----------------------------------------- */
    Network_Postprocess();

    Display_NetworkOutput(ts[1] - ts[0]);

#if TEST_MODE
    printf("digit=%d  predicted=%d  conf=%.0f%%  %s  (%lums)\r\n",
           current_digit, nn_ranking[0],
           nn_top1_output_class_proba * 100.0f,
           (nn_ranking[0] == current_digit) ? "OK" : "WRONG",
           ts[1] - ts[0]);
#else
    printf("[%d] %.0f%%  (%lums)\r\n",
           nn_ranking[0],
           nn_top1_output_class_proba * 100.0f,
           ts[1] - ts[0]);
#endif

#if DEBUG_DUMP_MS
    {
      static uint32_t last_dump_ms = 0;
      if (HAL_GetTick() - last_dump_ms >= DEBUG_DUMP_MS)
      {
        last_dump_ms = HAL_GetTick();
        Debug_DumpInput(nn_raw, net_in_dbg, nn_ranking[0],
                        (int)(nn_top1_output_class_proba * 100.0f));
      }
    }
#endif

    /* Discard nn_out region (used by pp_input and pp_outputs variables) to avoid Dcache evictions during nn inference */
    for (int i = 0; i < number_output; i++)
    {
      void *tmp = nn_out[i];
      SCB_InvalidateDCache_by_Addr(tmp, nn_out_len[i]);
    }
  }
}


static void Hardware_init(void)
{
  /* Power on ICACHE */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_ICACTIVE_Msk;

  /* Set back system and CPU clock source to HSI */
  __HAL_RCC_CPUCLK_CONFIG(RCC_CPUCLKSOURCE_HSI);
  __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_HSI);

  HAL_Init();

  SCB_EnableICache();

#if defined(USE_DCACHE)
  /* Power on DCACHE */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk;
  SCB_EnableDCache();
#endif

  SystemClock_Config();

  CONSOLE_Config();
  printf("\r\n[HW] UART OK\r\n");

  NPURam_enable();
  printf("[HW] NPU RAM OK\r\n");

  Fuse_Programming();
  printf("[HW] Fuse OK\r\n");

  NPUCache_config();
  printf("[HW] NPU Cache OK\r\n");

  /*** External RAM and NOR Flash *********************************************/
  BSP_XSPI_RAM_Init(0);
  BSP_XSPI_RAM_EnableMemoryMappedMode(0);
  printf("[HW] XSPI RAM OK\r\n");

  BSP_XSPI_NOR_Init_t NOR_Init;
  NOR_Init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
  NOR_Init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;
  BSP_XSPI_NOR_Init(0, &NOR_Init);
  BSP_XSPI_NOR_EnableMemoryMappedMode(0);
  printf("[HW] XSPI NOR OK\r\n");

  /* Set all required IPs as secure privileged */
  Security_Config();

  IAC_Config();
  set_clk_sleep_mode();

}

static void NeuralNetwork_init(uint32_t *nn_in_length, stai_ptr *nn_out, stai_size *number_output, int32_t nn_out_len[])
{
  stai_network_info info;
  int ret;

  /* initialize runtime */
  ret = stai_runtime_init();
  assert(ret == STAI_SUCCESS);
  /* init model instance */
  ret = stai_network_init(network_context);
  assert(ret == STAI_SUCCESS);

  ret = stai_network_get_info(network_context, &info);
  assert(ret == STAI_SUCCESS);
  assert(info.n_inputs == 1);
  *number_output = STAI_NETWORK_OUT_NUM;

  /* Get the input buffer size & address */
  *nn_in_length = info.inputs[0].size_bytes;
  ret = stai_network_get_inputs(network_context, &nn_in, (stai_size *)&info.n_inputs);
  assert(ret == STAI_SUCCESS);

  /* Get the output buffers size & address */
  ret = stai_network_get_outputs(network_context, nn_out, number_output);
  assert(ret == STAI_SUCCESS);
  for (int i = 0; i < *number_output; i++)
  {
    nn_out_len[i] = info.outputs[i].size_bytes;
  }
}

static void NPURam_enable(void)
{
  __HAL_RCC_NPU_CLK_ENABLE();
  __HAL_RCC_NPU_FORCE_RESET();
  __HAL_RCC_NPU_RELEASE_RESET();

  /* Enable NPU RAMs (4x448KB) */
  __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
  __HAL_RCC_RAMCFG_CLK_ENABLE();
  RAMCFG_HandleTypeDef hramcfg = {0};
  hramcfg.Instance =  RAMCFG_SRAM3_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
  hramcfg.Instance =  RAMCFG_SRAM4_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
  hramcfg.Instance =  RAMCFG_SRAM5_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
  hramcfg.Instance =  RAMCFG_SRAM6_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
}

static void set_clk_sleep_mode(void)
{
  /*** Enable sleep mode support during NPU inference *************************/
  /* Configure peripheral clocks to remain active during sleep mode */
  /* Keep all IP's enabled during WFE so they can wake up CPU. Fine tune
   * this if you want to save maximum power
   */
  __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();    /* For display frame buffer */
  __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();    /* For NN weights */
  __HAL_RCC_NPU_CLK_SLEEP_ENABLE();      /* For NN inference */
  __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE(); /* For NN inference */
  __HAL_RCC_LTDC_CLK_SLEEP_ENABLE();     /* For display */
  __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();    /* For display */
  __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();   /* For camera configuration retention */
  __HAL_RCC_CSI_CLK_SLEEP_ENABLE();      /* For camera configuration retention */

  __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE(); 

}

static void NPUCache_config(void)
{
  npu_cache_enable();
}

static void Security_Config(void)
{
  __HAL_RCC_RIFSC_CLK_ENABLE();
  RIMC_MasterConfig_t RIMC_master = {0};
  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1 , &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2 , &RIMC_master);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_NPU , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DMA2D , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDC   , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDCL1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDCL2 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

static void IAC_Config
(void)
{
/* Configure IAC to trap illegal access events */
  __HAL_RCC_IAC_CLK_ENABLE();
  __HAL_RCC_IAC_FORCE_RESET();
  __HAL_RCC_IAC_RELEASE_RESET();
}

void IAC_IRQHandler(void)
{
  while (1)
  {
  }
}

/**
* @brief Display Neural Network output classification results as well as other performances informations
*
* @param inference_ms inference time in ms
*/
static void Display_NetworkOutput(uint32_t inference_ms)
{
  int ret;

  ret = HAL_LTDC_SetAddress_NoReload(&hlcd_ltdc, (uint32_t) lcd_fg_buffer[lcd_fg_buffer_rd_idx], LTDC_LAYER_2);
  assert(ret == HAL_OK);

  UTIL_LCD_Clear(0);
  for (int i = 0; i < NB_CLASSES; i++)
  {
    UTIL_LCDEx_PrintfAt(0, LINE(i + 1), CENTER_MODE, "%s: %.0f%%",
                        classes_table[nn_ranking[i]],
                        ((float *)(pp_input))[i] * 100.0f);
  }
  UTIL_LCDEx_PrintfAt(0, LINE(12), CENTER_MODE, "Inference: %ums", inference_ms);

  Display_WelcomeScreen();

  SCB_CleanDCache_by_Addr(lcd_fg_buffer[lcd_fg_buffer_rd_idx], LCD_FG_FRAMEBUFFER_SIZE);
  ret = HAL_LTDC_ReloadLayer(&hlcd_ltdc, LTDC_RELOAD_VERTICAL_BLANKING, LTDC_LAYER_2);
  assert(ret == HAL_OK);
  lcd_fg_buffer_rd_idx = 1 - lcd_fg_buffer_rd_idx;
}

/**
 * @brief Run post-processing operation
 */
void Network_Postprocess(void)
{
  int ranking[NB_CLASSES];

  /**Perform ranking**/
  for (int i = 0; i < NB_CLASSES; i++)
  {
    ranking[i] = i;
  }

  Bubblesort((float *) (pp_input), ranking, NB_CLASSES);

  nn_top1_output_class_name  = classes_table[ranking[0]];
  nn_top1_output_class_proba = ((float *)(pp_input))[0];
  for (int i = 0; i < NB_CLASSES; i++)
    nn_ranking[i] = ranking[i];

  if (nn_top1_output_class_proba < 0.7f)
  {
    nn_top1_output_class_name = "Failed to classify the actual digit";
  }
}

/**
 * @brief Bubble sorting algorithm on probabilities
 * @param prob pointer to probabilities buffer
 * @param classes pointer to classes buffer
 * @param size numer of values
 */
static void Bubblesort(float *prob, int *classes, int size)
{
  float p;
  int c;

  for (int i = 0; i < size; i++)
  {
    for (int ii = 0; ii < size - i - 1; ii++)
    {
      if (prob[ii] < prob[ii + 1])
      {
        p = prob[ii];
        prob[ii] = prob[ii + 1];
        prob[ii + 1] = p;
        c = classes[ii];
        classes[ii] = classes[ii + 1];
        classes[ii + 1] = c;
      }
    }
  }
}

#if DEBUG_DUMP_MS
/**
  * @brief  Print the 32x32 camera frame (inverted grayscale, MNIST polarity) and
  *         the preprocessed int8 network input side by side as ASCII art.
  *         Bright = digit stroke. Lines are prefixed so they are easy to parse.
  */
static void Debug_DumpInput(const uint8_t *raw_rgb, const uint8_t *net_in, int pred, int conf)
{
  static const char ramp[] = " .:-=+*#%@";   /* 10 levels, dark -> bright */
  const int W = STAI_NETWORK_IN_1_WIDTH, H = STAI_NETWORK_IN_1_HEIGHT;
  char line[2 * 32 + 8];

  printf("[IMG] begin pred=%d conf=%d%% mode=%d  (left: camera, right: network input)\r\n", pred, conf, PREPROC_MODE);
  for (int y = 0; y < H; y++)
  {
    int k = 0;
    for (int x = 0; x < W; x++)
    {
      const uint8_t *px = &raw_rgb[3 * (y * W + x)];
      int gray = (299 * px[0] + 587 * px[1] + 114 * px[2]) / 1000;
      int v = 255 - gray;                                   /* invert: ink -> bright */
      line[k++] = ramp[(v * 9 + 127) / 255];
    }
    line[k++] = ' '; line[k++] = '|'; line[k++] = ' ';
    for (int x = 0; x < W; x++)
    {
      int v = net_in[y * W + x];                            /* 0..255 */
      line[k++] = ramp[(v * 9 + 127) / 255];
    }
    line[k] = 0;
    printf("[IMG] %s\r\n", line);
  }
  printf("[IMG] end\r\n");
}
#endif

static void LCD_init(void)
{
  BSP_LCD_Init(0, LCD_ORIENTATION_LANDSCAPE);

  /* Preview layer Init */
  LayerConfig.X0          = lcd_bg_area.X0;
  LayerConfig.Y0          = lcd_bg_area.Y0;
  LayerConfig.X1          = lcd_bg_area.X0 + lcd_bg_area.XSize;
  LayerConfig.Y1          = lcd_bg_area.Y0 + lcd_bg_area.YSize;
  LayerConfig.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
  LayerConfig.Address     = (uint32_t) lcd_bg_buffer;

  BSP_LCD_ConfigLayer(0, LTDC_LAYER_1, &LayerConfig);

  LayerConfig.X0 = lcd_fg_area.X0;
  LayerConfig.Y0 = lcd_fg_area.Y0;
  LayerConfig.X1 = lcd_fg_area.X0 + lcd_fg_area.XSize;
  LayerConfig.Y1 = lcd_fg_area.Y0 + lcd_fg_area.YSize;
  LayerConfig.PixelFormat = LCD_PIXEL_FORMAT_ARGB4444;
  LayerConfig.Address = (uint32_t) lcd_fg_buffer; /* External XSPI1 PSRAM */

  BSP_LCD_ConfigLayer(0, LTDC_LAYER_2, &LayerConfig);
  UTIL_LCD_SetFuncDriver(&LCD_Driver);
  UTIL_LCD_SetLayer(LTDC_LAYER_2);
  UTIL_LCD_Clear(0x00000000);
  UTIL_LCD_SetFont(&Font20);
  UTIL_LCD_SetBackColor(0x40000000);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
}

/**
 * @brief Displays a Welcome screen
 */
static void Display_WelcomeScreen(void)
{
  static uint32_t t0 = 0;
  if (t0 == 0)
    t0 = HAL_GetTick();

  if (HAL_GetTick() - t0 < 4000)
  {
    /* Draw logo */
    UTIL_LCD_FillRGBRect(300, 100, (uint8_t *) stlogo, 200, 107);

    /* Display welcome message */
    UTIL_LCDEx_PrintfAt(0, LINE(16), CENTER_MODE, "Image Classification");
    UTIL_LCDEx_PrintfAt(0, LINE(17), CENTER_MODE, WELCOME_MSG_1);
    UTIL_LCDEx_PrintfAt(0, LINE(18), CENTER_MODE, WELCOME_MSG_2);
  }
}

/**
  * @brief  DCMIPP Clock Config for DCMIPP.
  * @param  hdcmipp  DCMIPP Handle
  *         Being __weak it can be overwritten by the application
  * @retval HAL_status
  */
HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
  RCC_PeriphCLKInitTypeDef RCC_PeriphCLKInitStruct = {0};
  HAL_StatusTypeDef ret = HAL_OK;

  RCC_PeriphCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
  RCC_PeriphCLKInitStruct.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC17].ClockDivider = 3;
  ret = HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct);
  if (ret)
  {
    return ret;
  }

  RCC_PeriphCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CSI;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC18].ClockDivider = 40;
  ret = HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct);
  if (ret)
  {
    return ret;
  }

  return ret;
}

static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef RCC_PeriphCLKInitStruct = {0};

  /* Ensure VDDCORE=0.9V before increasing the system frequency */
  BSP_SMPS_Init(SMPS_VOLTAGE_OVERDRIVE);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;

  /* PLL1 = 64 x 25 / 2 = 800MHz */
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL1.PLLM = 2;
  RCC_OscInitStruct.PLL1.PLLN = 25;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  RCC_OscInitStruct.PLL1.PLLP1 = 1;
  RCC_OscInitStruct.PLL1.PLLP2 = 1;

  /* PLL2 = 64 x 125 / 8 = 1000MHz */
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL2.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL2.PLLM = 8;
  RCC_OscInitStruct.PLL2.PLLFractional = 0;
  RCC_OscInitStruct.PLL2.PLLN = 125;
  RCC_OscInitStruct.PLL2.PLLP1 = 1;
  RCC_OscInitStruct.PLL2.PLLP2 = 1;

  /* PLL3 = (64 x 225 / 8) / (1 * 2) = 900MHz */
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL3.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL3.PLLM = 8;
  RCC_OscInitStruct.PLL3.PLLN = 225;
  RCC_OscInitStruct.PLL3.PLLFractional = 0;
  RCC_OscInitStruct.PLL3.PLLP1 = 1;
  RCC_OscInitStruct.PLL3.PLLP2 = 2;

  /* PLL4 = (64 x 225 / 8) / (6 * 6) = 50 MHz */
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL4.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL4.PLLM = 8;
  RCC_OscInitStruct.PLL4.PLLFractional = 0;
  RCC_OscInitStruct.PLL4.PLLN = 225;
  RCC_OscInitStruct.PLL4.PLLP1 = 6;
  RCC_OscInitStruct.PLL4.PLLP2 = 6;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    while(1);
  }

  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK |
                                 RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 |
                                 RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 |
                                 RCC_CLOCKTYPE_PCLK5);

  /* CPU CLock (sysa_ck) = ic1_ck = PLL1 output/ic1_divider = 800 MHz */
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  RCC_ClkInitStruct.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC1Selection.ClockDivider = 1;

  /* AXI Clock (sysb_ck) = ic2_ck = PLL1 output/ic2_divider = 400 MHz */
  RCC_ClkInitStruct.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC2Selection.ClockDivider = 2;

  /* NPU Clock (sysc_ck) = ic6_ck = PLL2 output/ic6_divider = 1000 MHz */
  RCC_ClkInitStruct.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL2;
  RCC_ClkInitStruct.IC6Selection.ClockDivider = 1;

  /* AXISRAM3/4/5/6 Clock (sysd_ck) = ic11_ck = PLL3 output/ic11_divider = 900 MHz */
  RCC_ClkInitStruct.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL3;
  RCC_ClkInitStruct.IC11Selection.ClockDivider = 1;

  /* HCLK = sysb_ck / HCLK divider = 200 MHz */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;

  /* PCLKx = HCLK / PCLKx divider = 200 MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    while(1);
  }

  RCC_PeriphCLKInitStruct.PeriphClockSelection = 0;

  /* XSPI1 kernel clock (ck_ker_xspi1) = HCLK = 200MHz */
  RCC_PeriphCLKInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_XSPI1;
  RCC_PeriphCLKInitStruct.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;

  /* XSPI2 kernel clock (ck_ker_xspi1) = HCLK =  200MHz */
  RCC_PeriphCLKInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_XSPI2;
  RCC_PeriphCLKInitStruct.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;

  if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct) != HAL_OK)
  {
    while (1);
  }
}

static void CONSOLE_Config()
{
  GPIO_InitTypeDef gpio_init;

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

 /* DISCO & NUCLEO USART1 (PE5/PE6) */
  gpio_init.Mode      = GPIO_MODE_AF_PP;
  gpio_init.Pull      = GPIO_PULLUP;
  gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
  gpio_init.Pin       = GPIO_PIN_5 | GPIO_PIN_6;
  gpio_init.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOE, &gpio_init);

  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_8;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    while (1);
  }
}

int _write(int file, char *ptr, int len)
{
  HAL_StatusTypeDef status;

  if ((file != STDOUT_FILENO) && (file != STDERR_FILENO)) {
      errno = EBADF;
      return -1;
  }

  status = HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, ~0);

  return (status == HAL_OK ? len : 0);
}

void npu_cache_enable_clocks_and_reset(void)
{
  __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
  __HAL_RCC_CACHEAXI_CLK_ENABLE();
  __HAL_RCC_CACHEAXI_FORCE_RESET();
  __HAL_RCC_CACHEAXI_RELEASE_RESET();
}

void npu_cache_disable_clocks_and_reset(void)
{
  __HAL_RCC_CACHEAXIRAM_MEM_CLK_DISABLE();
  __HAL_RCC_CACHEAXI_CLK_DISABLE();
  __HAL_RCC_CACHEAXI_FORCE_RESET();
}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  UNUSED(file);
  UNUSED(line);
  __BKPT(0);
  while (1)
  {
  }
}

#endif
