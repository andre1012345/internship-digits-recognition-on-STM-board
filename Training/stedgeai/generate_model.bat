@echo off
REM ==========================================================================
REM  Generates the NPU C code for the quantized MNIST model with the LOCAL
REM  ST Edge AI Core 4.0 (same version as Middlewares/stedgeai-lib, ll_aton 1.1.3).
REM  Options = the ones the ST Edge AI Developer Cloud used for the June model
REM  (see the header of Model/network.c), default STM32N6 memory pool.
REM  Output: Training\stedgeai\out\   Log: Training\stedgeai\generate_log.txt
REM ==========================================================================
setlocal
cd /d "%~dp0"

set "STAI=C:\Dev\Tools\ST\STEdgeAI\4.0"
set "MODEL=..\model_2026-10\mnist_cnn_32x32_PerChannel_quant_mnist_calib_npz_2.onnx"

set "EXE="
for /f "delims=" %%F in ('dir /s /b "%STAI%\stedgeai.exe" 2^>nul') do if not defined EXE set "EXE=%%F"
if not defined EXE ( echo stedgeai.exe non trovato in %STAI% & pause & exit /b 1 )

if not exist stm32n6.mpool (
  set "MP="
  for /f "delims=" %%F in ('dir /s /b "%STAI%\Utilities\windows\stm32n6.mpool" 2^>nul') do if not defined MP set "MP=%%F"
  call :copympool
)
if not exist stm32n6.mpool ( echo stm32n6.mpool non trovato & pause & exit /b 1 )

echo Using: %EXE%
echo n| "%EXE%" generate --model "%MODEL%" --target stm32n6 --st-neural-art dc_equivalent@neuralart.json --name network --output out --workspace ws > generate_log.txt 2>&1
type generate_log.txt
echo.
echo ===== FINITO: scrivi "fatto" a Claude =====
pause
exit /b 0

:copympool
if defined MP copy "%MP%" stm32n6.mpool >nul
exit /b 0
