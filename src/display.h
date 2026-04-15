// display.h - Функції для роботи з TFT дисплеєм
#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_ST7735.h>
#include "config.h"

// === СТРУКТУРА АНІМАЦІЇ КАНАЛІВ ===
struct ChannelAnimation {
    bool active = false;           // Чи активна анімація
    int fromChannel = 0;           // З якого каналу
    int toChannel = 0;             // На який канал
    int currentStep = 0;           // Поточний крок анімації
    unsigned long lastStepTime = 0; // Час останнього кроку
    bool isIncreasing = true;      // Напрямок (true = вгору, false = вниз)
};

// === ГЛОБАЛЬНА ЗМІННА АНІМАЦІЇ ===
extern ChannelAnimation channelAnim;

// === ФУНКЦІЇ ДИСПЛЕЯ ===

// Ініціалізація статичного UI
void drawStaticUI();

// Малювання стрілочок циклів (outCold/outNormal/outHot)
void drawCycleArrows(AutoCycle cycle, bool isDayMode);

// Малювання каналів з анімацією
void drawChannels(int activeChannel);

// Малювання індикаторів (холод/підігрів)
void drawIndicators(bool coldLock, bool heat, int currentChannel);

// Оновлення дисплея (головна функція)
void updateDisplayNew(float temp, float hum, int channel, bool isDay, 
                      bool heat, bool coldLock, AutoCycle cycle, bool systemOn,
                      bool blynkConnected);
                                          
// Запуск анімації зміни каналів
void startChannelAnimation(int from, int to);

// Обробка анімації (викликати в loop)
void processChannelAnimation();

// === ДОПОМІЖНІ ФУНКЦІЇ ===

// Малювання заповненого трикутника (вершиною вниз)
void drawFilledTriangle(int centerX, int topY, int size, uint16_t color);

// Обчислення X-координати каналу
int getChannelX(int channelNum);

#endif // DISPLAY_H