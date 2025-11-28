/*******************************************************************************
 * OAISYS25 Badge - Hardware Configuration
 * Central pin definitions and constants for all modules
 ******************************************************************************/

#pragma once

//==============================================================================
// Display (ST7789 240x320)
//==============================================================================
#define LCD_CS          45
#define LCD_DC          42
#define LCD_BL          1
#define LCD_SCK         39
#define LCD_MOSI        38
#define LCD_MISO        40
#define LCD_WIDTH       240
#define LCD_HEIGHT      320

//==============================================================================
// IMU (QMI8658)
//==============================================================================
#define IMU_ADDRESS     0x6B
#define I2C_SDA         48
#define I2C_SCL         47

//==============================================================================
// Button
//==============================================================================
#define BTN_BOOT        0

//==============================================================================
// Microphones (INMP441 x2 - I2S0 RX)
//==============================================================================
#define MIC_BCK         2
#define MIC_WS          4
#define MIC_DIN         18
#define MIC_I2S_PORT    I2S_NUM_0

//==============================================================================
// Speaker (MAX98357A - I2S1 TX)
//==============================================================================
#define SPK_BCLK        6
#define SPK_LRC         7
#define SPK_DOUT        8
#define SPK_I2S_PORT    I2S_NUM_1

//==============================================================================
// SD Card (shared SPI with LCD)
//==============================================================================
#define SD_CS           41
#define SD_SCK          LCD_SCK
#define SD_MOSI         LCD_MOSI
#define SD_MISO         LCD_MISO

//==============================================================================
// Audio Configuration
//==============================================================================
#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_BITS_PER_SAMPLE   16
#define RECORD_DURATION_MS      5000  // 5 seconds recording

//==============================================================================
// ML Memory Pool
//==============================================================================
#define ML_POOL_SIZE            (6 * 1024 * 1024)  // 6MB for largest model (LLM)

//==============================================================================
// Timing Configuration
//==============================================================================
#define IDLE_SLEEP_MS           300000  // 5 minutes before deep sleep
#define WIFI_SCAN_INTERVAL_MS   30000   // Check WiFi every 30 seconds
#define ORIENTATION_POLL_MS     50      // IMU poll at 20Hz
#define ORIENTATION_DEBOUNCE_MS 1000    // 1 second stability for rotation

//==============================================================================
// Thresholds (defaults, can be overridden by SD card config)
//==============================================================================
#define DEFAULT_WAKE_THRESHOLD  0.5f
#define DEFAULT_EMBED_THRESHOLD 0.7f
#define DEFAULT_LLM_TEMPERATURE 0.8f
#define DEFAULT_LLM_TOPP        0.9f

//==============================================================================
// File Paths (SD card)
//==============================================================================
#define CONFIG_PATH             "/config.json"
#define VIDEO_PATH              "/media/logo.mjpeg"
#define WAKE_MODEL_PATH         "/models/wake_word.tflite"
#define WAKE_CONFIG_PATH        "/models/wake_word.json"
#define YAMNET_MODEL_PATH       "/models/yamnet.tflite"
#define PROJECTION_MODEL_PATH   "/models/projection.tflite"
#define LLM_MODEL_PATH          "/models/llm_model.bin"
#define TOKENIZER_PATH          "/models/tokenizer.bin"
#define EMBEDDINGS_PATH         "/data/embeddings.bin"
#define INTENTS_PATH            "/data/intents.txt"
#define STASH_DIR               "/stash"

//==============================================================================
// State Machine States
//==============================================================================
enum BadgeState {
    STATE_BOOT,
    STATE_LOGO_LOOP,
    STATE_DEEP_SLEEP,
    STATE_RECORDING,
    STATE_EMBEDDING,
    STATE_SIMILARITY,
    STATE_LLM_INFERENCE,
    STATE_DISPLAY_RESPONSE,
    STATE_TTS_OUTPUT,
    STATE_STASH_DATA,
    STATE_TTS_SORRY
};

//==============================================================================
// Text Overlay Colors (Matrix green theme)
//==============================================================================
#define TEXT_COLOR              0x07E0  // Green (RGB565)
#define TEXT_BG_COLOR           0x0000  // Black
#define TEXT_BORDER_COLOR       0x07E0  // Green border
