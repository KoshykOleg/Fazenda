// config.h - Конфігурація системи
#ifndef CONFIG_H
#define CONFIG_H

// === АВТОМАТИЧНІ ЦИКЛИ ===
enum AutoCycle { 
    outCold,    // +0.5°C offset, CH1↔CH2
    outNormal,  // 0.0°C offset, CH2↔CH3 (стартовий)
    outHot      // -0.5°C offset, CH3↔CH4
};

// === КОЛЬОРИ ДИСПЛЕЯ ===
// Основні (з main.ino)
#define C_ORANGE 0xFD20
#define C_CYAN   0xAF7D
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_RED    0xF800
#define C_GRAY   0x2104
#define C_DARK_GRAY  0x3186

// Стрілочки циклів
#define ARROW_COLD   0x001F  // Синій
#define ARROW_NORMAL 0x07E0  // Зелений (C_GREEN)
#define ARROW_HOT    0xF800  // Червоний (C_RED)

// === ПОЗИЦІЇ ЕЛЕМЕНТІВ НА ДИСПЛЕЇ ===
// Канали (квадрати)
#define CHANNELS_BASE_Y   100   // Центрована позиція каналів
#define CHANNEL_WIDTH     16
#define CHANNEL_HEIGHT    14
#define CHANNEL_SPACING   2

// Стрілочки циклів
#define ARROWS_Y          (CHANNELS_BASE_Y - 16)  // На кілька px вище каналів
#define ARROW_SIZE        11    // Розмір трикутника (середній 10-12px)

// Індикатори (кружечки)
#define INDICATOR_Y       (CHANNELS_BASE_Y + CHANNEL_HEIGHT/2)
#define INDICATOR_RADIUS  6
#define COLD_INDICATOR_X  10    // Зліва
#define HEAT_INDICATOR_X  150   // Справа

// === TIMING АНІМАЦІЇ ===
#define CHANNEL_ANIM_DELAY  70   
#endif // CONFIG_H