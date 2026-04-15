// display.cpp
#include "display.h"
#include <Arduino.h>

extern Adafruit_ST7735 tft;

ChannelAnimation channelAnim;

static float oldT = -99.9;
static float oldH = -1.0;
static int oldDisplayedChannel = -1;
static bool oldTooCold = false;
static bool oldHeat = false;
static bool oldDay = true;
static AutoCycle oldCycle = outNormal;
static bool oldSystemOn = true;

// === ДОПОМІЖНІ ФУНКЦІЇ ===
int getChannelX(int channelNum) {
    if (channelNum < 1 || channelNum > 4) return 0;
    
    int totalWidth = (CHANNEL_WIDTH * 4) + (CHANNEL_SPACING * 3);
    int startX = (160 - totalWidth) / 2;
    
    return startX + (channelNum - 1) * (CHANNEL_WIDTH + CHANNEL_SPACING);
}

void drawFilledTriangle(int centerX, int topY, int size, uint16_t color) {
    int halfBase = size / 2;
    
    for (int i = 0; i <= size; i++) {
        int width = halfBase - (i * halfBase) / size; 
        int y = topY + i;
        int x1 = centerX - width;
        int x2 = centerX + width;
        
        tft.drawFastHLine(x1, y, x2 - x1 + 1, color);
    }
}

// === ІНІЦІАЛІЗАЦІЯ СТАТИЧНОГО UI ===
void drawStaticUI() {
    tft.fillScreen(ST77XX_BLACK);
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(59, 2);
    tft.print("Fazenda");
    
    tft.fillCircle(110, 5, 2, C_RED);
    
    tft.drawFastHLine(0, 12, 160, 0x4208);

    drawChannels(0);
    drawCycleArrows(outNormal, true);
}

// === МАЛЮВАННЯ СТРІЛОЧОК ЦИКЛІВ ===
void drawCycleArrows(AutoCycle cycle, bool isDayMode) {
    tft.fillRect(0, ARROWS_Y, 160, ARROW_SIZE + 2, ST77XX_BLACK);
    
    if (!isDayMode) {
        tft.setTextSize(1);
        tft.setTextColor(C_YELLOW, ST77XX_BLACK);
        tft.setCursor(62, ARROWS_Y + 2);
        tft.print("NIGHT");
        return;
    }
    
    int ch1x = getChannelX(1);
    int ch2x = getChannelX(2);
    int ch3x = getChannelX(3);
    int ch4x = getChannelX(4);

    int offset = -0;
    
    int arrowColdX = ch1x + CHANNEL_WIDTH + offset;
    int arrowNormalX = ch2x + CHANNEL_WIDTH + offset;
    int arrowHotX = ch3x + CHANNEL_WIDTH + offset;
    
    drawFilledTriangle(arrowColdX, ARROWS_Y, ARROW_SIZE, 
                       (cycle == outCold) ? ARROW_COLD : C_DARK_GRAY);
    
    drawFilledTriangle(arrowNormalX, ARROWS_Y, ARROW_SIZE, 
                       (cycle == outNormal) ? ARROW_NORMAL : C_DARK_GRAY);
    
    drawFilledTriangle(arrowHotX, ARROWS_Y, ARROW_SIZE, 
                       (cycle == outHot) ? ARROW_HOT : C_DARK_GRAY);
}

// === МАЛЮВАННЯ КАНАЛІВ ===
void drawChannels(int activeChannel) {
    int ch1x = getChannelX(1);
    int ch2x = getChannelX(2);
    int ch3x = getChannelX(3);
    int ch4x = getChannelX(4);
    
    tft.fillRect(ch1x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                 (activeChannel >= 1) ? C_CYAN : C_DARK_GRAY);
    
    tft.fillRect(ch2x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                 (activeChannel >= 2) ? C_GREEN : C_DARK_GRAY);
    
    tft.fillRect(ch3x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                 (activeChannel >= 3) ? C_YELLOW : C_DARK_GRAY);
    
    tft.fillRect(ch4x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                 (activeChannel >= 4) ? C_RED : C_DARK_GRAY);
}

// === МАЛЮВАННЯ ІНДИКАТОРІВ ===
void drawIndicators(bool coldLock, bool heat, int currentChannel) {
    bool showCold = coldLock || (currentChannel == 0 && oldDisplayedChannel == 1);
    
    tft.fillCircle(COLD_INDICATOR_X, INDICATOR_Y, INDICATOR_RADIUS, 
                   showCold ? C_CYAN : C_GRAY);
    
    tft.fillCircle(HEAT_INDICATOR_X, INDICATOR_Y, INDICATOR_RADIUS, 
                   heat ? C_ORANGE : C_GRAY);
}

// === АНІМАЦІЯ КАНАЛІВ ===
void startChannelAnimation(int from, int to) {
    if (from == to) return;
    
    channelAnim.active = true;
    channelAnim.fromChannel = from;
    channelAnim.toChannel = to;
    channelAnim.isIncreasing = (to > from);
    channelAnim.currentStep = from;
    channelAnim.lastStepTime = millis();
    
    Serial.printf("[ANIM] Start: CH%d → CH%d\n", from, to);
}

void processChannelAnimation() {
    if (!channelAnim.active) return;
    
    unsigned long now = millis();
    if (now - channelAnim.lastStepTime < CHANNEL_ANIM_DELAY) return;
    
    channelAnim.lastStepTime = now;
    
    if (channelAnim.isIncreasing) {
        channelAnim.currentStep++;
        if (channelAnim.currentStep >= channelAnim.toChannel) {
            channelAnim.active = false;
            channelAnim.currentStep = channelAnim.toChannel;
        }
    } else {
        channelAnim.currentStep--;
        if (channelAnim.currentStep <= channelAnim.toChannel) {
            channelAnim.active = false;
            channelAnim.currentStep = channelAnim.toChannel;
        }
    }
    
    drawChannels(channelAnim.currentStep);
    
    if (!channelAnim.active) {
        Serial.printf("[ANIM] Finished at CH%d\n", channelAnim.currentStep);
    }
}

// === ГОЛОВНА ФУНКЦІЯ ОНОВЛЕННЯ ДИСПЛЕЯ ===
void updateDisplayNew(float t, float h, int channel, bool isDay, 
                      bool heat, bool coldLock, AutoCycle cycle, bool systemOn,
                      bool blynkConnected) {
    
    tft.setTextWrap(false);
    
    // 1. ТЕМПЕРАТУРА (центрована)
    if (fabsf(t - oldT) > 0.05 || isnan(t) != isnan(oldT)) {
        tft.fillRect(0, 18, 160, 24, ST77XX_BLACK);
        
        tft.setTextSize(3);
        tft.setTextColor(C_ORANGE, ST77XX_BLACK);
        
        if (isnan(t)) {
            int startX = (160 - 108) / 2;
            tft.setCursor(startX, 18);
            tft.print("T: ERR");
        } else {
            int approxWidth = 126;
            int startX = (160 - approxWidth) / 2;
            
            tft.setCursor(startX, 18);
            tft.printf("T:%.1f", t);
            
            int curX = tft.getCursorX();
            tft.setTextSize(1);
            tft.setCursor(curX + 1, 18);
            tft.print("o");
            tft.setTextSize(3);
            tft.setCursor(curX + 7, 18);
            tft.print("C");
        }
        oldT = t;
    }

    // 2. ВОЛОГІСТЬ
    if (fabsf(h - oldH) > 0.5 || isnan(h) != isnan(oldH)) {
        tft.fillRect(0, 48, 160, 24, ST77XX_BLACK);
        
        tft.setTextSize(3);
        tft.setTextColor(C_CYAN, ST77XX_BLACK);
        
        if (isnan(h)) {
            int startX = (160 - 108) / 2;
            tft.setCursor(startX, 48);
            tft.print("H: ERR");
        } else {
            int digits = (int)h >= 100 ? 6 : 5;
            int approxWidth = digits * 18;
            int startX = (160 - approxWidth) / 2;
            
            tft.setCursor(startX, 48);
            tft.printf("H:%d%%", (int)h);
        }
        oldH = h;
    }
    
    // 3. СТРІЛОЧКИ ЦИКЛІВ
    if (cycle != oldCycle || isDay != oldDay) {
        drawCycleArrows(cycle, isDay);
        oldCycle = cycle;
        oldDay = isDay;
    }
    
    // 4. КАНАЛИ
    if (!systemOn) {
        if (oldSystemOn) {
            tft.fillRect(0, CHANNELS_BASE_Y - 20, 160, 40, ST77XX_BLACK);
            
            int startX = (160 - 120) / 2;
            tft.setCursor(startX, CHANNELS_BASE_Y);
            tft.setTextSize(2);
            tft.setTextColor(C_RED);
            tft.print("SYSTEM OFF");
            oldSystemOn = false;
        }
    } else {
        if (!oldSystemOn) {
            tft.fillRect(0, CHANNELS_BASE_Y - 20, 160, 40, ST77XX_BLACK);
            oldSystemOn = true;
        }
        
        bool channelChanged = (channel != oldDisplayedChannel);
        if (channelChanged && !channelAnim.active) {
            startChannelAnimation(oldDisplayedChannel, channel);
            oldDisplayedChannel = channel;
        }

        // 5. ІНДИКАТОРИ
        if (coldLock != oldTooCold || heat != oldHeat || channelChanged) {
            drawIndicators(coldLock, heat, channel);
            oldTooCold = coldLock;
            oldHeat = heat;
        }
    }
    
    // 6. BLYNK СТАТУС
    static bool lastBlynkConnected = false;
    bool nowConnected = blynkConnected;
    if (nowConnected != lastBlynkConnected) {
        tft.fillCircle(110, 5, 2, nowConnected ? C_GREEN : C_RED);
        lastBlynkConnected = nowConnected;
    }
}