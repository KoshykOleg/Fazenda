#pragma once
#include "Arduino.h"
#include "DHT.h"
#include "SPIFFS.h"
#include "config.h"
#include "climate.h"
#include "logger.h"
#include "display.h"
#include <functional>
#include <random>
#include <string>
#include <vector>

extern DHT dht;
extern ClimateState climate;
extern Adafruit_ST7735 tft;
extern bool g_serialEcho;
extern bool g_timeValid;

std::vector<std::string> serialTail(size_t n);
void dumpSerialTail();

struct Physics {
    bool enabled = false;
    double T = 20.0;
    double H = 60.0;
    double Tout = 10.0;
    double Hout = 60.0;
    double ambK = 1.0 / 1800.0;
    double ventK = 1.0 / 900.0;
    double heatRate = 1.0 / 60.0;
    double solarMax = 0.025;
    double sun = 0.0;
    double humSrc = 0.02;
    double noiseT = 0.0;
    double noiseH = 0.0;
    void step(double dt, int ch, bool heat);
};

struct Invariants {
    bool enabled = false;
    bool checkDisplay = true;
    long viol[16] = {0};
    std::vector<std::string> examples[16];
    double dayZeroSince = -1;
    double coldFanSince = -1;
    double dayHeatSince = -1;
    double nightHighSince = -1;
    double heatWrongSince = -1;
    double emergencySince = -1;
    double dispMismatchSince = -1;
    double lastBoostOff = -1e9;
    double lastChange = -1e9;
    int prevCh = -99;
    bool prevBoost = false;
    long channelChanges = 0;
    long kickstarts = 0;
    long heatSwitches = 0;
    long coldlockActivations = 0;
    long cycleChanges = 0;
    bool prevHeat = false;
    bool prevCold = false;
    int prevCycle = -1;
    double lastModeChange = -1e9;
    double lastDhtChange = -1e9;
    double lastSysChange = -1e9;
    bool prevDay = true;
    bool prevDhtErr = false;
    bool prevSys = true;
    void reset();
    void report(int id, const std::string& msg);
    void evaluate();
    static const char* name(int id);
};

struct Sim {
    const unsigned long CLIMATE_CHECK_INTERVAL = 10000;
    unsigned long lastReadTime = 0;
    bool displayEnabled = true;
    bool blynkConnected = false;
    unsigned long stepMs = 100;
    Physics phy;
    Invariants inv;
    std::mt19937 rng{12345};
    long controlTicks = 0;
    std::function<void()> onStep;

    int lastShownFan = -1;
    bool lastShownHeat = false;
    bool lastShownTooCold = false;
    float lastShownT = -999.0f;
    float lastShownH = -999.0f;
    bool lastShownDay = true;
    AutoCycle lastShownCycle = outNormal;
    HumCycle lastShownHumCycle = humLow;
    bool lastShownSystemOn = true;
    unsigned long lastFullRedraw = 0;

    struct Cfg {
        float t = 25.0f;
        float h = 50.0f;
        int light = 0;
        float setTemp = 25.0f;
        float setHum = 50.0f;
        float hyst = 0.1f;
        float humHys = 5.0f;
        bool sysOn = true;
        bool storage = false;
    };

    void boot(const Cfg& c);
    void step(unsigned long dt);
    void advance(unsigned long ms);
    void advanceFine(unsigned long ms, unsigned long dt);
    unsigned long msToNextTick() const;
    void displayLoop();

    void v0Boost(bool on);
    void v5SetTemp(float v);
    void v6Hum(float v);
    void v10System(bool on);
    void v12HumHys(int v);
    void v15Hyst(int v);

    static void setT(float t) { dht._fakeTemp = t; dht._fakeError = false; }
    static void setH(float h) { dht._fakeHum = h; }
    static void setTH(float t, float h) { setT(t); setH(h); }
    static void setLight(int v) { g_fakeLight = v; }
    static void dhtError(bool e) { dht._fakeError = e; }

    static int fanPin();
    static bool heatPin();
    static int displayedChannel(bool* dayLayout);
};

double simSeconds();
const char* cycleName(int c);

void expectInvariants(const Sim& s, const char* ctx);
