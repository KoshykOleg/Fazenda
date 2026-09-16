#include "sim.h"
#include "tf.h"
#include <deque>

unsigned long g_fakeMillis = 0;
int g_pinState[64];
int g_fakeLight = 0;
SerialClass Serial;
bool g_serialEcho = false;
bool g_timeValid = false;
static const time_t kEpochBase = 1789500000;

FakeFsState g_fs;
SPIFFSClass SPIFFS;

DHT dht;
ClimateState climate;
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

static std::deque<std::string> g_tail;
static std::string g_partial;

void serialWrite(const char* s) {
    if (g_serialEcho) fputs(s, stdout);
    for (const char* p = s; *p; ++p) {
        if (*p == '\n') {
            g_tail.push_back(g_partial);
            g_partial.clear();
            if (g_tail.size() > 40) g_tail.pop_front();
        } else {
            g_partial += *p;
        }
    }
}

std::vector<std::string> serialTail(size_t n) {
    std::vector<std::string> v;
    size_t start = g_tail.size() > n ? g_tail.size() - n : 0;
    for (size_t i = start; i < g_tail.size(); i++) v.push_back(g_tail[i]);
    return v;
}

void dumpSerialTail() {
    printf("        --- serial tail @ %.1fs: T=%.2f H=%.1f ch=%d pend=%d heat=%d day=%d cold=%d/%d cyc=%s kick=%d boost=%d sys=%d dhtErr=%d\n",
        simSeconds(), dht._fakeTemp, dht._fakeHum, climate.currentActiveChannel, climate.pendingChannel,
        climate.currentHeatState, climate.isDay, climate.coldLockMode, climate.tooColdLock,
        cycleName(climate.activeCycle), climate.kickstartActive, climate.manualBoost, climate.systemOn,
        climate.dhtRetryCount);
    for (const auto& l : serialTail(8)) printf("        | %s\n", l.c_str());
}

bool getLocalTime(struct tm* info, uint32_t) {
    if (!g_timeValid) return false;
    time_t t = kEpochBase + (time_t)(millis() / 1000);
    gmtime_r(&t, info);
    return true;
}

double simSeconds() { return millis() / 1000.0; }

const char* cycleName(int c) {
    return c == outCold ? "outCold" : c == outNormal ? "outNormal" : c == outHot ? "outHot" : "?";
}

void Physics::step(double dt, int ch, bool heat) {
    double vent = ventK * ch;
    double dT = (ambK + vent) * (Tout - T) + (heat ? heatRate : 0.0) + solarMax * sun;
    T += dT * dt;
    double dH = humSrc - (ambK + vent) * (H - Hout);
    H += dH * dt;
    if (H < 0) H = 0;
    if (H > 100) H = 100;
}

int Sim::fanPin() {
    const int pins[4] = {RELAY_CH1_PIN, RELAY_CH2_PIN, RELAY_CH3_PIN, RELAY_CH4_PIN};
    int active = 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (g_pinState[pins[i]] == LOW) { active = i + 1; n++; }
    }
    return n > 1 ? -1 : active;
}

bool Sim::heatPin() { return g_pinState[RELAY_HEAT_PIN] == LOW; }

int Sim::displayedChannel(bool* dayLayout) {
    bool day = tft.px(getChannelX(1, 4) + 1, CHANNELS_BASE_Y + 1) != ST77XX_BLACK;
    if (dayLayout) *dayLayout = day;
    int maxCh = day ? 4 : 3;
    int n = 0;
    for (int i = 1; i <= maxCh; i++) {
        uint16_t c = tft.px(getChannelX(i, maxCh) + CHANNEL_WIDTH / 2, CHANNELS_BASE_Y + CHANNEL_HEIGHT / 2);
        if (c != C_DARK_GRAY && c != ST77XX_BLACK) n++;
    }
    return n;
}

void Sim::boot(const Cfg& c) {
    g_fakeMillis = 0;
    for (int i = 0; i < 64; i++) g_pinState[i] = HIGH;
    if (displayEnabled) {
        tft.initR(INITR_BLACKTAB);
        tft.setRotation(1);
        drawStaticUI();
    }
    dht._fakeTemp = c.t;
    dht._fakeHum = c.h;
    dht._fakeError = false;
    g_fakeLight = c.light;
    delay(2000);

    float initialT = dht.readTemperature();
    float initialH = dht.readHumidity();
    if (isnan(initialT) || isnan(initialH)) {
        delay(2500);
        initialT = dht.readTemperature();
        initialH = dht.readHumidity();
    }
    int initialLight = analogRead(LIGHT_SENSOR_PIN);
    bool initialIsDay = (initialLight < 2000);

    climateInit(&climate);
    climate.isDay = initialIsDay;

    logger.storageAvailable = c.storage ? initSPIFFS() : false;
    if (logger.storageAvailable) logEvent("BOOT", "System started");

    climate.set_temp_day = constrain(c.setTemp, 10.0, 35.0);
    climate.set_hum_limit = constrain(c.setHum, 30.0, 90.0);
    climate.hysteresis = constrain(c.hyst, 0.1, 4.0);
    climate.humHys = constrain(c.humHys, 1.0, 10.0);
    climate.systemOn = c.sysOn;
    climate.lastValidT = initialT;
    climate.lastValidH = initialH;

    if (displayEnabled) {
        updateDisplayNew(climate.lastValidT, climate.lastValidH,
                         climate.currentActiveChannel, climate.isDay,
                         climate.currentHeatState, climate.tooColdLock,
                         climate.activeCycle, climate.humCycle,
                         climate.systemOn, false);
    }
    delay(1000);
    lastReadTime = millis() - CLIMATE_CHECK_INTERVAL;

    lastShownFan = -1;
    lastShownHeat = false;
    lastShownTooCold = false;
    lastShownT = -999.0f;
    lastShownH = -999.0f;
    lastShownDay = !climate.isDay;
    lastShownCycle = outNormal;
    lastShownHumCycle = humLow;
    lastShownSystemOn = true;
    lastFullRedraw = 0;
    controlTicks = 0;
    inv.reset();
    if (phy.enabled) {
        phy.T = c.t;
        phy.H = c.h;
    }
}

void Sim::displayLoop() {
    climate.lastBlynkState = blynkConnected;
    bool tChanged = (isnan(climate.lastValidT) != isnan(lastShownT)) ||
                    (!isnan(climate.lastValidT) && fabsf(climate.lastValidT - lastShownT) > 0.05);
    bool hChanged = (isnan(climate.lastValidH) != isnan(lastShownH)) ||
                    (!isnan(climate.lastValidH) && fabsf(climate.lastValidH - lastShownH) > 0.5);

    if (climate.currentActiveChannel != lastShownFan ||
        climate.currentHeatState != lastShownHeat ||
        climate.tooColdLock != lastShownTooCold ||
        tChanged || hChanged ||
        climate.isDay != lastShownDay ||
        climate.activeCycle != lastShownCycle ||
        climate.humCycle != lastShownHumCycle ||
        climate.systemOn != lastShownSystemOn) {

        updateDisplayNew(climate.lastValidT, climate.lastValidH,
                         climate.currentActiveChannel, climate.isDay,
                         climate.currentHeatState, climate.tooColdLock,
                         climate.activeCycle, climate.humCycle,
                         climate.systemOn, blynkConnected);

        lastShownFan = climate.currentActiveChannel;
        lastShownHeat = climate.currentHeatState;
        lastShownTooCold = climate.tooColdLock;
        lastShownT = climate.lastValidT;
        lastShownH = climate.lastValidH;
        lastShownDay = climate.isDay;
        lastShownCycle = climate.activeCycle;
        lastShownHumCycle = climate.humCycle;
        lastShownSystemOn = climate.systemOn;
    }

    if (millis() - lastFullRedraw > 600000UL) {
        channelAnim.active = false;
        tft.initR(INITR_BLACKTAB);
        tft.setRotation(1);
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(59, 2);
        tft.print("Fazenda");
        tft.drawFastHLine(0, 12, 160, 0x4208);
        resetDisplayCache();
        tft.fillCircle(110, 5, 2, climate.lastBlynkState ? C_GREEN : C_RED);
        lastShownFan = -99;
        lastShownT = -999.0f;
        lastShownH = -999.0f;
        lastShownDay = !climate.isDay;
        lastShownSystemOn = !climate.systemOn;
        lastFullRedraw = millis();
    }
}

void Sim::step(unsigned long dt) {
    g_fakeMillis += dt;
    if (phy.enabled) {
        phy.step(dt / 1000.0, climate.currentActiveChannel, climate.currentHeatState);
    }

    handleRelayQueue(&climate);
    if (displayEnabled) processChannelAnimation(climate.isDay);

    if (climate.kickstartActive && (millis() - climate.kickstartTime >= KICKSTART_DURATION)) {
        climate.kickstartActive = false;
        if (climate.targetChannelAfterKick != climate.currentActiveChannel) {
            setFanChannel(&climate, climate.targetChannelAfterKick);
        }
    }

    if (millis() - lastReadTime >= CLIMATE_CHECK_INTERVAL) {
        lastReadTime = millis();
        if (phy.enabled) {
            std::normal_distribution<double> nT(0.0, phy.noiseT > 0 ? phy.noiseT : 1e-9);
            std::normal_distribution<double> nH(0.0, phy.noiseH > 0 ? phy.noiseH : 1e-9);
            double tr = phy.T + (phy.noiseT > 0 ? nT(rng) : 0.0);
            double hr = phy.H + (phy.noiseH > 0 ? nH(rng) : 0.0);
            dht._fakeTemp = (float)(std::round(tr * 10.0) / 10.0);
            dht._fakeHum = (float)(std::round(hr * 10.0) / 10.0);
        }
        runClimateControl(&climate);
        controlTicks++;
    }

    if (displayEnabled) displayLoop();
    if (inv.enabled) inv.evaluate();
    if (onStep) onStep();
}

void Sim::advance(unsigned long ms) { advanceFine(ms, stepMs); }

void Sim::advanceFine(unsigned long ms, unsigned long dt) {
    unsigned long end = millis() + ms;
    while (millis() < end) {
        unsigned long d = std::min(dt, end - millis());
        step(d);
    }
}

unsigned long Sim::msToNextTick() const {
    unsigned long next = lastReadTime + CLIMATE_CHECK_INTERVAL;
    return next > millis() ? next - millis() : 0;
}

void Sim::v0Boost(bool newBoost) {
    if (newBoost && !climate.systemOn) return;
    if (!newBoost && climate.manualBoost) inv.lastBoostOff = simSeconds();
    climate.manualBoost = newBoost;
    if (climate.manualBoost) startFanWithKick(&climate, 4);
}

void Sim::v5SetTemp(float v) { climate.set_temp_day = constrain(v, 10.0, 35.0); }

void Sim::v6Hum(float v) {
    climate.set_hum_limit = constrain(v, 30.0, 90.0);
    if (!climate.isDay) {
        unsigned long now = millis();
        if (now - lastReadTime < 2000) lastReadTime = now - 8000;
        else lastReadTime = 0;
    }
}

void Sim::v10System(bool on) {
    climate.systemOn = on;
    if (!climate.systemOn) {
        setFanChannel(&climate, 0);
        heatControl(&climate, false);
        if (climate.isDay) {
            climate.activeCycle = outNormal;
            climate.autoOffset = 0.0;
            climate.bootCycleSelected = false;
        }
    }
}

void Sim::v12HumHys(int v) { climate.humHys = constrain(v, 1, 10); }
void Sim::v15Hyst(int v) { climate.hysteresis = constrain(v, 1, 50) / 10.0; }

const char* Invariants::name(int id) {
    switch (id) {
        case 1: return "I1 relay exclusivity (<=1 fan relay ON)";
        case 2: return "I2 relay pins == currentActiveChannel";
        case 3: return "I3 heat pin == currentHeatState";
        case 4: return "I4 coldLockMode == tooColdLock";
        case 5: return "I5 channel range 0..4";
        case 6: return "I6 DAY floor: fan >= CH1 outside ColdLock (>25s)";
        case 7: return "I7 ColdLock: fan OFF (>1s)";
        case 8: return "I8 heater: ON at T<=20.0, OFF at T>=21.0 (>11s)";
        case 9: return "I9 systemOn=false: fan & heat OFF";
        case 10: return "I10 NIGHT max CH3 (>1s)";
        case 11: return "I11 display == real state (>3s)";
        case 12: return "I12 DHT emergency: DAY CH3 / NIGHT CH1 / boost CH4, heater OFF (>11s)";
        default: return "?";
    }
}

void Invariants::reset() {
    for (int i = 0; i < 16; i++) { viol[i] = 0; examples[i].clear(); }
    dayZeroSince = coldFanSince = dayHeatSince = nightHighSince = dispMismatchSince = heatWrongSince = emergencySince = -1;
    lastBoostOff = -1e9;
    lastChange = -1e9;
    lastModeChange = lastDhtChange = lastSysChange = -1e9;
    prevDay = climate.isDay;
    prevDhtErr = climate.dhtRetryCount > 0;
    prevSys = climate.systemOn;
    prevCh = -99;
    prevBoost = false;
    channelChanges = kickstarts = heatSwitches = coldlockActivations = cycleChanges = 0;
    prevHeat = false;
    prevCold = false;
    prevCycle = -1;
}

void Invariants::report(int id, const std::string& msg) {
    viol[id]++;
    if (examples[id].size() < 3) {
        char buf[256];
        double now = simSeconds();
        snprintf(buf, sizeof(buf), "@%.1fs T=%.1f H=%.1f ch=%d heat=%d day=%d cold=%d/%d boost=%d sys=%d dht=%d [since mode %.0fs, dht %.0fs, sys %.0fs]: %s",
            now, dht._fakeTemp, dht._fakeHum, climate.currentActiveChannel, climate.currentHeatState,
            climate.isDay, climate.coldLockMode, climate.tooColdLock, climate.manualBoost, climate.systemOn,
            climate.dhtRetryCount, now - lastModeChange, now - lastDhtChange, now - lastSysChange, msg.c_str());
        examples[id].push_back(buf);
    }
}

static bool duration(double& since, bool cond, double limit) {
    double now = simSeconds();
    if (!cond) { since = -1; return false; }
    if (since < 0) since = now;
    if (now - since > limit) { since = now; return true; }
    return false;
}

void Invariants::evaluate() {
    double now = simSeconds();
    int ch = climate.currentActiveChannel;
    int pin = Sim::fanPin();

    if (climate.isDay != prevDay) { prevDay = climate.isDay; lastModeChange = now; }
    if ((climate.dhtRetryCount > 0) != prevDhtErr) { prevDhtErr = climate.dhtRetryCount > 0; lastDhtChange = now; }
    if (climate.systemOn != prevSys) { prevSys = climate.systemOn; lastSysChange = now; }
    if (ch != prevCh) {
        if (prevCh == 0 && ch > 0) kickstarts++;
        if (prevCh != -99) channelChanges++;
        prevCh = ch;
        lastChange = now;
    }
    if (climate.currentHeatState != prevHeat) { heatSwitches++; prevHeat = climate.currentHeatState; }
    if (climate.coldLockMode && !prevCold) coldlockActivations++;
    prevCold = climate.coldLockMode;
    if ((int)climate.activeCycle != prevCycle) {
        if (prevCycle != -1) cycleChanges++;
        prevCycle = climate.activeCycle;
    }

    if (pin == -1) report(1, "two or more fan relays LOW");
    if (climate.pendingChannel == -1) {
        if (pin != ch) report(2, tf::fmt("pins show CH%d", pin));
    } else if (pin != 0 && pin != -1) {
        report(2, tf::fmt("pending %d but pin CH%d active (no break)", climate.pendingChannel, pin));
    }
    if (Sim::heatPin() != climate.currentHeatState) report(3, "heat pin mismatch");
    if (climate.coldLockMode != climate.tooColdLock) report(4, "flags differ");
    if (ch < 0 || ch > 4) report(5, "out of range");

    bool dhtOk = climate.dhtRetryCount == 0;
    bool afterBoost = (now - lastBoostOff) < 11.0;

    bool c6 = climate.isDay && climate.systemOn && !climate.manualBoost && dhtOk && !climate.coldLockMode && ch == 0;
    if (duration(dayZeroSince, c6, 25.0)) report(6, "fan OFF in DAY without ColdLock");

    bool c7 = climate.coldLockMode && climate.systemOn && !climate.manualBoost && dhtOk && !afterBoost && ch != 0;
    if (duration(coldFanSince, c7, 1.0)) report(7, "fan ON during ColdLock");

    float lt = climate.lastValidT;
    bool known = climate.systemOn && dhtOk && !isnan(lt);
    bool c8 = known && ((lt <= 20.0f && !climate.currentHeatState) || (lt >= 21.0f && climate.currentHeatState));
    if (duration(heatWrongSince, c8, 11.0)) report(8, tf::fmt("heater %s at last T=%.1f", climate.currentHeatState ? "ON" : "OFF", lt));

    bool emerg = climate.systemOn && climate.dhtRetryCount >= 3;
    int want = climate.manualBoost ? 4 : (climate.isDay ? 3 : 1);
    bool c12 = emerg && !climate.kickstartActive && (ch != want || climate.currentHeatState);
    if (duration(emergencySince, c12, 11.0)) report(12, tf::fmt("expected CH%d + heater OFF", want));

    if (!climate.systemOn && (ch != 0 || climate.currentHeatState || pin != 0)) report(9, "something ON while system OFF");

    bool c10 = !climate.isDay && !climate.manualBoost && dhtOk && !afterBoost && !climate.kickstartActive && ch > 3;
    if (duration(nightHighSince, c10, 1.0)) report(10, "CH4 at night");

    if (checkDisplay && climate.systemOn) {
        bool dayLayout = true;
        int shown = Sim::displayedChannel(&dayLayout);
        bool quiet = !channelAnim.active && climate.pendingChannel == -1 && (now - lastChange) > 1.0;
        int expect = climate.isDay ? ch : (ch > 3 ? 3 : ch);
        bool mismatch = quiet && (shown != expect || dayLayout != climate.isDay);
        if (duration(dispMismatchSince, mismatch, 3.0))
            report(11, tf::fmt("screen CH%d/%s vs real CH%d/%s", shown, dayLayout ? "DAY" : "NIGHT", ch, climate.isDay ? "DAY" : "NIGHT"));
    } else {
        dispMismatchSince = -1;
    }
}

void expectInvariants(const Sim& s, const char* ctx) {
    bool any = false;
    for (int i = 1; i <= 12; i++) {
        if (s.inv.viol[i] > 0) {
            any = true;
            CHECK(false, "%s: %s -> %ld violations, e.g. %s", ctx, Invariants::name(i), s.inv.viol[i],
                  s.inv.examples[i].empty() ? "" : s.inv.examples[i][0].c_str());
        }
    }
    if (!any) CHECK(true, "%s: all invariants hold", ctx);
}
