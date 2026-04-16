// display.h 
#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_ST7735.h>
#include "config.h"

// === СТРУКТУРА АНІМАЦІЇ КАНАЛІВ ===
struct ChannelAnimation {
    bool active = false;
    int fromChannel = 0;
    int toChannel = 0;
    int currentStep = 0;
    unsigned long lastStepTime = 0;
    bool isIncreasing = true;
};

extern ChannelAnimation channelAnim;

// === ФУНКЦІЇ ДИСПЛЕЯ ===

void drawStaticUI();

// ✏️ ОНОВЛЕНА СИГНАТУРА
void drawCycleArrows(AutoCycle dayCycle, HumCycle humCycle, bool isDayMode);

// ✏️ ОНОВЛЕНА СИГНАТУРА
void drawChannels(int activeChannel, int maxChannels);

void drawIndicators(bool coldLock, bool heat, int currentChannel);

// ✏️ ОНОВЛЕНА СИГНАТУРА 
void updateDisplayNew(float temp, float hum, int channel, bool isDay, 
                      bool heat, bool coldLock, 
                      AutoCycle dayCycle, HumCycle humCycle,
                      bool systemOn, bool blynkConnected);

void startChannelAnimation(int from, int to);
void processChannelAnimation();

// === ДОПОМІЖНІ ФУНКЦІЇ ===
void drawFilledTriangle(int centerX, int topY, int size, uint16_t color);
int getChannelX(int channelNum, int maxChannels);

#endif // DISPLAY_H