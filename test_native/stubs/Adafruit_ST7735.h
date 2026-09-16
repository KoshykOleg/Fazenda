#pragma once
#include <Arduino.h>
#include <string>
#include <vector>

#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF
#define INITR_BLACKTAB 0x02

class Adafruit_ST7735 {
public:
    static constexpr int W = 160;
    static constexpr int H = 128;
    uint16_t fb[H][W];
    int cursorX = 0;
    int cursorY = 0;
    int textSize = 1;
    uint16_t textColor = ST77XX_WHITE;
    long initCount = 0;
    long oobPixels = 0;
    int firstOobX = 0;
    int firstOobY = 0;
    std::vector<std::string> printed;

    Adafruit_ST7735(int, int, int, int, int) { fillScreen(ST77XX_BLACK); }
    void initR(uint8_t) { initCount++; }
    void setRotation(uint8_t) {}
    void setTextWrap(bool) {}
    void setTextSize(int s) { textSize = s; }
    void setTextColor(uint16_t c) { textColor = c; }
    void setTextColor(uint16_t c, uint16_t) { textColor = c; }
    void setCursor(int x, int y) { cursorX = x; cursorY = y; }
    int getCursorX() const { return cursorX; }
    void print(const char* s) {
        if (printed.size() > 64) printed.erase(printed.begin());
        printed.push_back(s);
        cursorX += 6 * textSize * (int)strlen(s);
    }
    __attribute__((format(printf, 2, 3)))
    void printf(const char* fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        print(buf);
    }
    void drawPixel(int x, int y, uint16_t c) {
        if (x < 0 || y < 0 || x >= W || y >= H) {
            if (oobPixels == 0) { firstOobX = x; firstOobY = y; }
            oobPixels++;
            return;
        }
        fb[y][x] = c;
    }
    void fillScreen(uint16_t c) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) fb[y][x] = c;
    }
    void drawFastHLine(int x, int y, int w, uint16_t c) {
        for (int i = 0; i < w; i++) drawPixel(x + i, y, c);
    }
    void fillRect(int x, int y, int w, int h, uint16_t c) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) drawPixel(x + i, y + j, c);
    }
    void fillCircle(int x0, int y0, int r, uint16_t c) {
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r) drawPixel(x0 + dx, y0 + dy, c);
    }
    uint16_t px(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        return fb[y][x];
    }
    long count(int x, int y, int w, int h, uint16_t c) const {
        long n = 0;
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++)
                if (px(i, j) == c) n++;
        return n;
    }
    bool printedContains(const std::string& s) const {
        for (const auto& p : printed) if (p.find(s) != std::string::npos) return true;
        return false;
    }
};
