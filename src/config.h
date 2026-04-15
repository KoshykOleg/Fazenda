// config.h
#ifndef CONFIG_H
#define CONFIG_H

// === ПІНИ РЕЛЕ ===
#define RELAY_CH1_PIN  14
#define RELAY_CH2_PIN  27
#define RELAY_CH3_PIN  26
#define RELAY_CH4_PIN  32
#define RELAY_HEAT_PIN 19

// === ПІНИ СЕНСОРІВ ===
#define DHTPIN           4
#define DHTTYPE          DHT22
#define LIGHT_SENSOR_PIN 34

// === ПІНИ TFT ===
#define TFT_CS   5
#define TFT_RST  15
#define TFT_DC   2
#define TFT_MOSI 23
#define TFT_SCLK 22

// === АВТОМАТИЧНІ ЦИКЛИ ===
enum AutoCycle { 
    outCold,
    outNormal,
    outHot
};

// === КОЛЬОРИ ДИСПЛЕЯ ===
#define C_ORANGE    0xFD20
#define C_CYAN      0xAF7D
#define C_GREEN     0x07E0
#define C_YELLOW    0xFFE0
#define C_RED       0xF800
#define C_GRAY      0x2104
#define C_DARK_GRAY 0x3186

#define ARROW_COLD   0x001F
#define ARROW_NORMAL 0x07E0
#define ARROW_HOT    0xF800

// === ПОЗИЦІЇ ЕЛЕМЕНТІВ НА ДИСПЛЕЇ ===
#define CHANNELS_BASE_Y  100
#define CHANNEL_WIDTH    16
#define CHANNEL_HEIGHT   14
#define CHANNEL_SPACING  2

#define ARROWS_Y         (CHANNELS_BASE_Y - 16)
#define ARROW_SIZE       11

#define INDICATOR_Y       (CHANNELS_BASE_Y + CHANNEL_HEIGHT/2)
#define INDICATOR_RADIUS  6
#define COLD_INDICATOR_X  10
#define HEAT_INDICATOR_X  150

// === TIMING АНІМАЦІЇ ===
#define CHANNEL_ANIM_DELAY 70

// === DEBUG МАКРОС ===
#define DEBUG_CLIMATE 1  // 1=debug ON, 0=debug OFF

#if DEBUG_CLIMATE
  #define DBG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
  #define DBG_PRINTLN(msg) Serial.println(msg)
#else
  #define DBG(fmt, ...)
  #define DBG_PRINTLN(msg)
#endif

#endif // CONFIG_H