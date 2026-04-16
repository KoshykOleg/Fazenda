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
static AutoCycle oldDayCycle = outNormal;      // ➕ ДОДАНО
static HumCycle oldHumCycle = humLow;           // ➕ ДОДАНО
static bool oldSystemOn = true;

// === ДОПОМІЖНІ ФУНКЦІЇ ===
int getChannelX(int channelNum, int maxChannels) {  // ✏️ додано maxChannels
    if (channelNum < 1 || channelNum > maxChannels) return 0;
    
    int totalWidth = (CHANNEL_WIDTH * maxChannels) + (CHANNEL_SPACING * (maxChannels - 1));
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

    drawChannels(0, 4);  // ✏️ ВИПРАВЛЕНО
    drawCycleArrows(outNormal, humLow, true);  // ✏️ ВИПРАВЛЕНО
}

// === МАЛЮВАННЯ СТРІЛОЧОК ЦИКЛІВ ===
void drawCycleArrows(AutoCycle dayCycle, HumCycle humCycle, bool isDayMode) {
    tft.fillRect(0, ARROWS_Y, 160, ARROW_SIZE + 2, ST77XX_BLACK);
    
    if (isDayMode) {
        // ДЕНЬ: 3 стрілки між 4-ма каналами
        int ch1x = getChannelX(1, 4);
        int ch2x = getChannelX(2, 4);
        int ch3x = getChannelX(3, 4);
        
        int arrowColdX = ch1x + CHANNEL_WIDTH;
        int arrowNormalX = ch2x + CHANNEL_WIDTH;
        int arrowHotX = ch3x + CHANNEL_WIDTH;
        
        drawFilledTriangle(arrowColdX, ARROWS_Y, ARROW_SIZE, 
                           (dayCycle == outCold) ? ARROW_COLD : C_DARK_GRAY);
        
        drawFilledTriangle(arrowNormalX, ARROWS_Y, ARROW_SIZE, 
                           (dayCycle == outNormal) ? ARROW_NORMAL : C_DARK_GRAY);
        
        drawFilledTriangle(arrowHotX, ARROWS_Y, ARROW_SIZE, 
                           (dayCycle == outHot) ? ARROW_HOT : C_DARK_GRAY);
    } else {
        // НІЧ: 2 стрілки між 3-ма каналами
        int ch1x = getChannelX(1, 3);
        int ch2x = getChannelX(2, 3);
        
        int arrowLowX = ch1x + CHANNEL_WIDTH;
        int arrowHighX = ch2x + CHANNEL_WIDTH;
        
        drawFilledTriangle(arrowLowX, ARROWS_Y, ARROW_SIZE, 
                           (humCycle == humLow) ? ARROW_HUM_LOW : C_DARK_GRAY);
        
        drawFilledTriangle(arrowHighX, ARROWS_Y, ARROW_SIZE, 
                           (humCycle == humHigh) ? ARROW_HUM_HIGH : C_DARK_GRAY);
    }
}

// === МАЛЮВАННЯ КАНАЛІВ ===
void drawChannels(int activeChannel, int maxChannels) {
    // Очистка зони каналів
    tft.fillRect(0, CHANNELS_BASE_Y, 160, CHANNEL_HEIGHT, ST77XX_BLACK);
    
    if (maxChannels == 4) {
        // ДЕНЬ: 4 канали
        int ch1x = getChannelX(1, 4);
        int ch2x = getChannelX(2, 4);
        int ch3x = getChannelX(3, 4);
        int ch4x = getChannelX(4, 4);
        
        tft.fillRect(ch1x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 1) ? C_CYAN : C_DARK_GRAY);
        
        tft.fillRect(ch2x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 2) ? C_GREEN : C_DARK_GRAY);
        
        tft.fillRect(ch3x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 3) ? C_YELLOW : C_DARK_GRAY);
        
        tft.fillRect(ch4x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 4) ? C_RED : C_DARK_GRAY);
    } else {
        // НІЧ: 3 канали
        int ch1x = getChannelX(1, 3);
        int ch2x = getChannelX(2, 3);
        int ch3x = getChannelX(3, 3);
        
        tft.fillRect(ch1x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 1) ? C_GREEN : C_DARK_GRAY);
        
        tft.fillRect(ch2x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 2) ? C_YELLOW : C_DARK_GRAY);
        
        tft.fillRect(ch3x, CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, 
                     (activeChannel >= 3) ? C_RED : C_DARK_GRAY);
    }
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
    
    int maxChannels = oldDay ? 4 : 3;
    drawChannels(channelAnim.currentStep, maxChannels);
    
    if (!channelAnim.active) {
        Serial.printf("[ANIM] Finished at CH%d\n", channelAnim.currentStep);
    }
}

// === ГОЛОВНА ФУНКЦІЯ ОНОВЛЕННЯ ДИСПЛЕЯ ===
void updateDisplayNew(float t, float h, int channel, bool isDay, 
                      bool heat, bool coldLock, 
                      AutoCycle dayCycle, HumCycle humCycle,
                      bool systemOn, bool blynkConnected) {
    
    tft.setTextWrap(false);
    
    // 1. ТЕМПЕРАТУРА
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
    
    // 3. ПЕРЕХІД ДЕНЬ/НІЧ
    if (isDay != oldDay) {
        tft.fillRect(0, ARROWS_Y, 160, ARROW_SIZE + CHANNEL_HEIGHT + 20, ST77XX_BLACK);
        
        int maxChannels = isDay ? 4 : 3;
        drawCycleArrows(dayCycle, humCycle, isDay);
        drawChannels(channel, maxChannels);
        
        oldDay = isDay;
        oldDayCycle = dayCycle;
        oldHumCycle = humCycle;
        oldDisplayedChannel = channel;
        
        Serial.printf("[DISPLAY] Mode changed: %s (CH=%d)\n", isDay ? "DAY" : "NIGHT", maxChannels);
    }
    // 4. СТРІЛОЧКИ ЦИКЛІВ
    else if ((isDay && dayCycle != oldDayCycle) || (!isDay && humCycle != oldHumCycle)) {
        drawCycleArrows(dayCycle, humCycle, isDay);
        oldDayCycle = dayCycle;
        oldHumCycle = humCycle;
    }
    
    // 5. СИСТЕМА OFF
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
            int maxChannels = isDay ? 4 : 3;
            drawCycleArrows(dayCycle, humCycle, isDay);
            drawChannels(channel, maxChannels);
            oldSystemOn = true;
        }
        
        // 6. КАНАЛИ
        bool channelChanged = (channel != oldDisplayedChannel);
        if (channelChanged && !channelAnim.active) {
            startChannelAnimation(oldDisplayedChannel, channel);
            oldDisplayedChannel = channel;
        }

        // 7. ІНДИКАТОРИ
        if (coldLock != oldTooCold || heat != oldHeat || channelChanged) {
            drawIndicators(coldLock, heat, channel);
            oldTooCold = coldLock;
            oldHeat = heat;
        }
    }
    
    // 8. BLYNK СТАТУС
    static bool lastBlynkConnected = false;
    if (blynkConnected != lastBlynkConnected) {
        tft.fillCircle(110, 5, 2, blynkConnected ? C_GREEN : C_RED);
        lastBlynkConnected = blynkConnected;
    }
}