#pragma once
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <ctime>
#include <string>

using std::isnan;

#define HIGH 1
#define LOW  0
#define INPUT  0
#define OUTPUT 1

extern unsigned long g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }
inline void delay(unsigned long ms) { g_fakeMillis += ms; }

extern int g_pinState[64];
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int val) { if (pin >= 0 && pin < 64) g_pinState[pin] = val; }
inline int digitalRead(int pin) { return (pin >= 0 && pin < 64) ? g_pinState[pin] : LOW; }

extern int g_fakeLight;
inline int analogRead(int) { return g_fakeLight; }

template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v); }

bool getLocalTime(struct tm* info, uint32_t ms = 5000);

void serialWrite(const char* s);

class SerialClass {
public:
    void begin(long) {}
    int available() { return 0; }
    int read() { return -1; }
    void print(const char* s) { serialWrite(s); }
    void println() { serialWrite("\n"); }
    void println(const char* s) { serialWrite(s); serialWrite("\n"); }
    __attribute__((format(printf, 2, 3)))
    void printf(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        serialWrite(buf);
    }
};
extern SerialClass Serial;
