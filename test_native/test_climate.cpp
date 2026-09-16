#include "sim.h"
#include "tf.h"
#include <algorithm>

static Sim S;

static Sim::Cfg dayCfg(float t, float h = 50.0f) {
    Sim::Cfg c;
    c.t = t;
    c.h = h;
    c.light = 300;
    return c;
}

static Sim::Cfg nightCfg(float t, float h = 50.0f) {
    Sim::Cfg c;
    c.t = t;
    c.h = h;
    c.light = 3800;
    return c;
}

static void bootInv(const Sim::Cfg& c) {
    S.boot(c);
    S.inv.enabled = true;
}

static void toTick() { S.advanceFine(S.msToNextTick() + 10, 10); }

static int ch() { return climate.currentActiveChannel; }

static void unitSuite() {
    tf::suite("CLIMATE: unit functions");

    tf::run("selectCycleOnBoot: temperature bands", [] {
        const float sets[] = {18.0f, 25.0f, 30.0f};
        const float d[] = {-3.0f, -1.0f, -0.9f, 0.0f, 0.5f, 0.6f, 3.0f};
        for (float s : sets) {
            for (float dd : d) {
                ClimateState st;
                climateInit(&st);
                st.set_temp_day = s;
                float t = s + dd;
                selectCycleOnBoot(&st, t);
                AutoCycle e = (t <= s - 1.0) ? outCold : (t <= s + 0.5) ? outNormal : outHot;
                float eo = e == outCold ? 0.5f : e == outNormal ? 0.0f : -0.5f;
                CHECK(st.activeCycle == e && st.autoOffset == eo, "set=%.1f T=%.1f -> %s (%+.1f), got %s (%+.1f)",
                      s, t, cycleName(e), eo, cycleName(st.activeCycle), st.autoOffset);
            }
        }
    });

    tf::run("checkCycleTransition: direction-aware, all cycle x old x new", [] {
        struct R { AutoCycle from; int oldCh; int newCh; AutoCycle to; };
        const R expected[] = {
            {outNormal, 2, 1, outCold},
            {outNormal, 3, 4, outHot},
            {outNormal, 1, 4, outHot},
            {outCold, 2, 3, outNormal},
            {outCold, 1, 3, outNormal},
            {outHot, 3, 2, outNormal},
            {outHot, 4, 2, outNormal},
        };
        const AutoCycle cycles[] = {outCold, outNormal, outHot};
        int checked = 0;
        for (AutoCycle from : cycles) {
            for (int o = 0; o <= 4; o++) {
                for (int n = 0; n <= 4; n++) {
                    if (o == n) continue;
                    AutoCycle to = from;
                    bool up = n > o, down = n < o;
                    if (n != 0) {
                        if (from == outNormal && n == 1 && down) to = outCold;
                        if (from == outNormal && n == 4 && up) to = outHot;
                        if (from == outCold && n == 3 && up) to = outNormal;
                        if (from == outHot && n == 2 && down) to = outNormal;
                    }
                    for (const R& r : expected)
                        if (r.from == from && r.oldCh == o && r.newCh == n && r.to != to)
                            CHECK(false, "spec table mismatch for %s CH%d->CH%d", cycleName(from), o, n);
                    ClimateState st;
                    climateInit(&st);
                    st.isDay = true;
                    st.activeCycle = from;
                    st.autoOffset = from == outCold ? 0.5f : from == outNormal ? 0.0f : -0.5f;
                    checkCycleTransition(&st, o, n);
                    float eo = to == outCold ? 0.5f : to == outNormal ? 0.0f : -0.5f;
                    if (st.activeCycle != to || st.autoOffset != eo) {
                        CHECK(false, "%s CH%d->CH%d: expected %s, got %s", cycleName(from), o, n, cycleName(to), cycleName(st.activeCycle));
                    }
                    checked++;
                }
            }
        }
        CHECK(checked == 60, "%d combinations checked, mismatches reported above", checked);
        const R blocked[] = {
            {outHot, 1, 2, outHot},
            {outCold, 4, 3, outCold},
            {outHot, 2, 1, outHot},
            {outCold, 2, 1, outCold},
            {outNormal, 4, 3, outNormal},
        };
        for (const R& r : blocked) {
            ClimateState st;
            climateInit(&st);
            st.isDay = true;
            st.activeCycle = r.from;
            checkCycleTransition(&st, r.oldCh, r.newCh);
            CHECK(st.activeCycle == r.to, "no switch for %s CH%d->CH%d (wrong direction / from OFF)", cycleName(r.from), r.oldCh, r.newCh);
        }
        ClimateState st;
        climateInit(&st);
        st.isDay = false;
        st.activeCycle = outNormal;
        checkCycleTransition(&st, 2, 1);
        CHECK(st.activeCycle == outNormal, "NIGHT: cycle transitions are ignored");
    });

    tf::run("relay queue: break-before-make, 150ms delay, last request wins", [] {
        for (int i = 0; i < 64; i++) g_pinState[i] = HIGH;
        g_fakeMillis = 1000;
        ClimateState st;
        climateInit(&st);
        setFanChannel(&st, 2);
        CHECK(Sim::fanPin() == 0 && st.currentActiveChannel == 0 && st.pendingChannel == 2, "right after request: all relays OFF, pending=2");
        g_fakeMillis += 149;
        handleRelayQueue(&st);
        CHECK(Sim::fanPin() == 0, "+149ms: still OFF");
        g_fakeMillis += 1;
        handleRelayQueue(&st);
        CHECK(Sim::fanPin() == 2 && st.currentActiveChannel == 2 && st.pendingChannel == -1, "+150ms: CH2 ON");
        setFanChannel(&st, 2);
        CHECK(st.pendingChannel == -1 && Sim::fanPin() == 2, "repeat request for the same channel: no relay blip");
        setFanChannel(&st, 3);
        g_fakeMillis += 50;
        setFanChannel(&st, 4);
        g_fakeMillis += 149;
        handleRelayQueue(&st);
        CHECK(Sim::fanPin() == 0, "new request restarts the 150ms delay");
        g_fakeMillis += 1;
        handleRelayQueue(&st);
        CHECK(Sim::fanPin() == 4 && st.currentActiveChannel == 4, "last request wins (CH4)");
        setFanChannel(&st, 0);
        CHECK(Sim::fanPin() == 0 && st.currentActiveChannel == 0 && st.pendingChannel == -1, "CH0 applies instantly");
        for (int i = 1; i <= 4; i++) {
            setFanChannel(&st, i);
            g_fakeMillis += 150;
            handleRelayQueue(&st);
            CHECK(Sim::fanPin() == i, "CH%d drives only its own relay pin", i);
        }
    });

    tf::run("heatControl: active-LOW pin", [] {
        for (int i = 0; i < 64; i++) g_pinState[i] = HIGH;
        ClimateState st;
        climateInit(&st);
        heatControl(&st, true);
        CHECK(Sim::heatPin() && st.currentHeatState, "ON -> pin LOW");
        heatControl(&st, false);
        CHECK(!Sim::heatPin() && !st.currentHeatState, "OFF -> pin HIGH");
    });

    tf::run("kickstart: from 0 runs CH4 for 5s, then target", [] {
        S.displayEnabled = false;
        S.boot(dayCfg(22.0f));
        S.advanceFine(250, 10);
        CHECK(Sim::fanPin() == 4 && climate.kickstartActive && climate.targetChannelAfterKick == 1,
              "right after start: CH4 kick, target CH1");
        S.advanceFine(4600, 10);
        CHECK(Sim::fanPin() == 4, "~4.8s: still CH4");
        S.advanceFine(500, 10);
        CHECK(Sim::fanPin() == 1 && !climate.kickstartActive, "~5.3s: CH1");

        ClimateState st;
        climateInit(&st);
        st.currentActiveChannel = 2;
        startFanWithKick(&st, 3);
        CHECK(!st.kickstartActive && st.pendingChannel == 3, "from CH2: direct switch, no kick");
        climateInit(&st);
        startFanWithKick(&st, 1);
        startFanWithKick(&st, 3);
        CHECK(st.kickstartActive && st.targetChannelAfterKick == 3, "new target during kick replaces old target");
    });
}

static void daySuite() {
    tf::suite("CLIMATE: DAY logic (new rules)");

    struct Row { float t; int ch; AutoCycle cyc; };
    const Row rows[] = {
        {18.5f, 1, outCold}, {24.0f, 1, outCold}, {24.2f, 1, outNormal}, {25.0f, 2, outNormal},
        {25.5f, 3, outNormal}, {25.7f, 4, outHot}, {28.0f, 4, outHot},
    };
    for (const Row& r : rows) {
        tf::run(tf::fmt("cold start T=%.1f (set 25.0) -> CH%d, %s", r.t, r.ch, cycleName(r.cyc)), [r] {
            bootInv(dayCfg(r.t));
            S.advance(90000);
            CHECK(ch() == r.ch, "steady channel CH%d (got CH%d)", r.ch, ch());
            CHECK(climate.activeCycle == r.cyc, "cycle %s (got %s)", cycleName(r.cyc), cycleName(climate.activeCycle));
            CHECK(climate.currentHeatState == (r.t <= 20.0f), "heater %s", r.t <= 20.0f ? "ON (T <= 20)" : "OFF");
            expectInvariants(S, "cold start");
        });
    }

    tf::run("cold start T=24.2: cycle chosen by temperature survives landing on CH1", [] {
        bootInv(dayCfg(24.2f));
        S.advance(600000);
        CHECK(climate.activeCycle == outNormal && ch() == 1, "10 min later: CH1 + outNormal (got CH%d + %s)", ch(), cycleName(climate.activeCycle));
    });

    tf::run("hot start T=28: cycle must stay outHot while climbing CH1->CH4", [] {
        bootInv(dayCfg(28.0f));
        S.advance(90000);
        CHECK(S.inv.cycleChanges == 0, "cycle changes during climb: %ld (expected 0)", S.inv.cycleChanges);
        CHECK(ch() == 4 && climate.activeCycle == outHot, "final CH4 + outHot");
        Sim::setT(22.0f);
        S.advance(120000);
        CHECK(ch() == 1 && climate.activeCycle == outCold, "cool-down 28->22: CH4..CH1, outHot->outNormal->outCold (got CH%d + %s, %ld changes)",
              ch(), cycleName(climate.activeCycle), S.inv.cycleChanges);
        CHECK(S.inv.cycleChanges == 2, "exactly 2 cycle changes on the way down (got %ld)", S.inv.cycleChanges);
    });

    tf::run("DAY 18.5C (above ColdLock): fan CH1, heater ON (T <= 20)", [] {
        bootInv(dayCfg(18.5f));
        S.advance(300000);
        CHECK(ch() == 1 && climate.currentHeatState && !climate.coldLockMode, "CH1, heater ON, no ColdLock (CH%d heat=%d)", ch(), climate.currentHeatState);
        expectInvariants(S, "18.5C");
    });

    const int hysts[] = {1, 5, 10, 20};
    for (int raw : hysts) {
        tf::run(tf::fmt("CH1 floor sweep: hyst=%.1f x set 18/20/22/25/28/30, ramp down to ColdLock edge", raw / 10.0), [raw] {
            S.displayEnabled = false;
            const float sets[] = {18.0f, 20.0f, 22.0f, 25.0f, 28.0f, 30.0f};
            for (float set : sets) {
                Sim::Cfg c = dayCfg(set + 1.0f);
                c.setTemp = set;
                c.hyst = raw / 10.0f;
                S.boot(c);
                S.inv.enabled = true;
                S.inv.checkDisplay = false;
                int minCh = 9;
                bool locked = false;
                S.onStep = [&] {
                    if (millis() > 12000) minCh = std::min(minCh, ch());
                    locked = locked || climate.coldLockMode;
                };
                float floorT = 17.55f;
                float t = set + 1.0f;
                S.advance(20000);
                while (t > floorT) {
                    t = std::max(floorT, t - 0.1f);
                    Sim::setT(t);
                    S.advance(10000);
                }
                S.advance(120000);
                S.onStep = nullptr;
                CHECK(minCh >= 1 && !locked, "set=%.0f: min channel CH%d, ColdLock=%d (down to %.2fC)", set, minCh, locked, floorT);
                expectInvariants(S, tf::fmt("set=%.0f", set).c_str());
            }
        });
    }

    for (int raw : {1, 10}) {
        tf::run(tf::fmt("ColdLock + heater boundaries (V15 hyst=%.1f must not shift them)", raw / 10.0), [raw] {
            Sim::Cfg c = dayCfg(20.5f);
            c.hyst = raw / 10.0f;
            bootInv(c);
            S.advance(15000);
            CHECK(!climate.currentHeatState, "T=20.5 from OFF: heater stays OFF");
            Sim::setT(20.05f);
            S.advance(20000);
            CHECK(!climate.currentHeatState, "T=20.05: heater still OFF");
            Sim::setT(17.52f);
            S.advance(30000);
            CHECK(!climate.coldLockMode && ch() >= 1, "T=17.52: no ColdLock, fan CH%d", ch());
            CHECK(climate.currentHeatState, "T=17.52: heater ON");
            Sim::setT(17.48f);
            toTick();
            CHECK(climate.coldLockMode && climate.tooColdLock && ch() == 0 && Sim::fanPin() == 0, "T=17.48: ColdLock at the tick, fan OFF immediately");
            Sim::setT(19.48f);
            S.advance(60000);
            CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "T=19.48: still locked, heater ON");
            Sim::setT(19.5f);
            toTick();
            CHECK(!climate.coldLockMode && !climate.tooColdLock, "T=19.50: unlocked at the tick");
            S.advance(6000);
            CHECK(ch() >= 1, "fan back ON within 6s (kickstart), CH%d", ch());
            CHECK(climate.currentHeatState, "heater keeps heating after unlock (below 21.0)");
            Sim::setT(20.95f);
            S.advance(20000);
            CHECK(climate.currentHeatState, "T=20.95: heater still ON");
            Sim::setT(21.0f);
            toTick();
            CHECK(!climate.currentHeatState && !Sim::heatPin(), "T=21.00: heater OFF at the tick");
            expectInvariants(S, "boundaries");
        });
    }

    struct ExitRow { float set; AutoCycle exp; };
    const ExitRow exits[] = {{25.0f, outCold}, {19.5f, outNormal}, {18.0f, outHot}};
    for (const ExitRow& e : exits) {
        tf::run(tf::fmt("ColdLock exit at 19.7C with set=%.1f -> cycle %s (by temperature)", e.set, cycleName(e.exp)), [e] {
            Sim::Cfg c = dayCfg(17.0f);
            c.setTemp = e.set;
            bootInv(c);
            S.advance(15000);
            CHECK(climate.coldLockMode, "locked at 17.0C");
            Sim::setT(19.7f);
            toTick();
            CHECK(!climate.coldLockMode && climate.activeCycle == e.exp, "after unlock: %s (got %s)", cycleName(e.exp), cycleName(climate.activeCycle));
            S.advance(6000);
            CHECK(ch() >= 1 && climate.activeCycle == e.exp, "after landing on CH%d: still %s (got %s)", ch(), cycleName(e.exp), cycleName(climate.activeCycle));
        });
    }

    tf::run("low setpoint (set=19): ColdLock heats; heater vs ventilation above setpoint", [] {
        Sim::Cfg c = dayCfg(17.0f);
        c.setTemp = 19.0f;
        bootInv(c);
        S.advance(60000);
        CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "ColdLock: fan OFF, heater ON");
        Sim::setT(20.0f);
        S.advance(90000);
        if (climate.currentHeatState && ch() >= 2) {
            OBSERVE("set_temp=19: at T=20.0 the heater runs (target 21.0) while ventilation cools on CH%d - they work against each other for setpoints below ~21.5C", ch());
        }
        CHECK(true, "scenario executed");
    });

    tf::run("V5 setpoint change while running: climbs / descends, never 0", [] {
        bootInv(dayCfg(22.0f));
        S.advance(30000);
        CHECK(ch() == 1, "start CH1");
        S.v5SetTemp(20.0f);
        S.advance(80000);
        CHECK(ch() == 4, "set 20 -> CH4 (got CH%d)", ch());
        S.v5SetTemp(30.0f);
        S.advance(80000);
        CHECK(ch() == 1, "set 30 -> back to CH1, not 0 (got CH%d)", ch());
        expectInvariants(S, "V5");
    });

    tf::run("hysteresis vs sensor noise (+-0.2C) near T3 threshold: switches per hour", [] {
        S.displayEnabled = false;
        for (int raw : {1, 2, 5}) {
            Sim::Cfg c = dayCfg(25.1f);
            c.hyst = raw / 10.0f;
            S.boot(c);
            S.inv.enabled = true;
            S.inv.checkDisplay = false;
            std::mt19937 r(7);
            std::uniform_int_distribution<int> d(-2, 2);
            S.onStep = [&] { Sim::setT(25.1f + d(r) * 0.1f); };
            S.advance(120000);
            long base = S.inv.channelChanges;
            S.advance(3600000);
            S.onStep = nullptr;
            long n = S.inv.channelChanges - base;
            INFO("hyst=%.1f: %ld channel switches in 1h", raw / 10.0, n);
            if (raw == 1 && n > 20) {
                OBSERVE("hyst=0.1 with +-0.2C sensor noise: %ld fan switches per hour near a threshold (hyst 0.2/0.5 reduce it)", n);
            }
            CHECK(n < 360, "hyst=%.1f: fewer than one switch per tick on average (%ld)", raw / 10.0, n);
        }
    });
}

static void nightSuite() {
    tf::suite("CLIMATE: NIGHT logic");

    for (bool day : {true, false}) {
        tf::run(tf::fmt("%s heater thermostat: ON at <=20.0, OFF at >=21.0", day ? "DAY" : "NIGHT"), [day] {
            Sim::Cfg c = day ? dayCfg(22.0f) : nightCfg(22.0f, 40.0f);
            c.hyst = 1.0f;
            bootInv(c);
            auto at = [](float t, bool exp) {
                Sim::setT(t);
                S.advance(20000);
                CHECK(climate.currentHeatState == exp && Sim::heatPin() == exp, "T=%.2f -> heater %s", t, exp ? "ON" : "OFF");
            };
            S.advance(15000);
            CHECK(!climate.currentHeatState, "T=22.0: OFF");
            at(20.05f, false);
            at(20.0f, true);
            at(20.95f, true);
            at(21.0f, false);
            at(20.5f, false);
            at(18.0f, true);
            CHECK(ch() >= 1, "fan keeps running while heater works (CH%d)", ch());
            expectInvariants(S, "thermostat");
        });
    }

    tf::run("NIGHT humidity zones, humHys=5, set_hum=50", [] {
        Sim::Cfg c = nightCfg(22.0f, 40.0f);
        bootInv(c);
        S.advance(20000);
        CHECK(ch() == 1, "start H=40 -> CH1 (got CH%d)", ch());
        auto at = [](float h, int exp, const char* why) {
            Sim::setH(h);
            S.advance(20000);
            CHECK(ch() == exp, "H=%.1f -> CH%d (%s), got CH%d", h, exp, why, ch());
        };
        at(54.9f, 1, "below 50+5");
        at(55.0f, 2, ">= set+humHys");
        at(45.1f, 2, "above set-humHys");
        at(45.0f, 1, "<= set-humHys");
        at(55.0f, 2, "up again");
        at(59.9f, 2, "below 55+5");
        at(60.0f, 3, ">= H_HIGH+humHys");
        CHECK(climate.humCycle == humHigh, "humCycle -> humHigh on CH3");
        at(50.1f, 3, "above H_HIGH-humHys");
        at(50.0f, 2, "<= H_HIGH-humHys");
        CHECK(climate.humCycle == humHigh, "humHigh kept on CH2");
        at(45.0f, 1, "back to CH1");
        CHECK(climate.humCycle == humLow, "humCycle -> humLow on CH1");
        at(39.0f, 1, "stays CH1 lower down");
        expectInvariants(S, "humidity");
    });

    tf::run("NIGHT start from CH0 with H>=set -> CH2", [] {
        bootInv(nightCfg(22.0f, 50.0f));
        S.advance(30000);
        CHECK(ch() == 2, "CH2 (got CH%d)", ch());
    });

    tf::run("NIGHT ColdLock enter/exit (regression)", [] {
        bootInv(nightCfg(17.0f, 60.0f));
        S.advance(20000);
        CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "locked: fan OFF, heater ON");
        Sim::setT(19.7f);
        S.advance(20000);
        CHECK(!climate.coldLockMode && ch() >= 1, "unlocked at 19.7: humidity control resumes (CH%d)", ch());
        CHECK(climate.currentHeatState, "heater keeps running up to 21C after unlock");
        CHECK(climate.activeCycle == outNormal, "day cycle not touched at night");
        expectInvariants(S, "night coldlock");
    });
}

static void sensorSuite() {
    tf::suite("CLIMATE: sensors, light, modes");

    tf::run("DHT error DAY: ERR on screen at once, fan waits for the 3rd error, then CH3 + heater OFF", [] {
        Sim::Cfg c = dayCfg(19.8f);
        c.storage = true;
        bootInv(c);
        S.advance(20000);
        CHECK(ch() == 1 && climate.currentHeatState, "start: CH1, heater ON (19.8C)");
        Sim::dhtError(true);
        toTick();
        S.advanceFine(300, 10);
        CHECK(isnan(climate.lastValidT) && tft.printedContains("T: ERR"), "1st error: 'T: ERR' on screen immediately");
        CHECK(ch() == 1 && climate.currentHeatState, "1st error: fan and heater unchanged (CH%d heat=%d)", ch(), climate.currentHeatState);
        toTick();
        S.advanceFine(300, 10);
        CHECK(ch() == 1 && climate.currentHeatState && climate.dhtRetryCount == 2, "2nd error: unchanged");
        toTick();
        S.advanceFine(300, 10);
        CHECK(ch() == 3 && !climate.currentHeatState, "3rd error: CH3, heater OFF (CH%d heat=%d)", ch(), climate.currentHeatState);
        CHECK(logger.dht_errors == 1, "dht_errors=1");
        S.advance(1200000);
        CHECK(climate.dhtRetryCount == 100, "retry counter saturates at 100 (got %d)", climate.dhtRetryCount);
        CHECK(logger.dht_errors == 1 && ch() == 3, "one DHT_ERROR per burst, still CH3");
        Sim::dhtError(false);
        Sim::setT(22.0f);
        toTick();
        CHECK(climate.dhtRetryCount == 0 && !isnan(climate.lastValidT), "recovered");
        S.advance(40000);
        CHECK(ch() == 1, "stepped back down to CH1, not 0 (got CH%d)", ch());
        const std::string& log = g_fs.files["/climate.log"];
        size_t n = 0;
        for (size_t p = log.find("DHT_ERROR"); p != std::string::npos; p = log.find("DHT_ERROR", p + 1)) n++;
        CHECK(n == 1, "log contains exactly one DHT_ERROR (got %zu)", n);
        expectInvariants(S, "dht day");
    });

    tf::run("DHT single/double glitches: no fan or heater changes", [] {
        bootInv(dayCfg(19.8f));
        S.advance(30000);
        long changes = S.inv.channelChanges, heat = S.inv.heatSwitches;
        for (int i = 0; i < 10; i++) {
            Sim::dhtError(true);
            toTick();
            if (i % 2) toTick();
            Sim::dhtError(false);
            toTick();
            S.advance(20000);
        }
        CHECK(S.inv.channelChanges == changes && S.inv.heatSwitches == heat, "fan switches +%ld, heater switches +%ld (expected 0)",
              S.inv.channelChanges - changes, S.inv.heatSwitches - heat);
        expectInvariants(S, "glitches");
    });

    tf::run("DHT error NIGHT during ColdLock: CH1 via kickstart, heater OFF", [] {
        bootInv(nightCfg(17.0f, 60.0f));
        S.advance(30000);
        CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "locked, heater ON");
        Sim::dhtError(true);
        toTick();
        toTick();
        CHECK(ch() == 0 && climate.currentHeatState, "after 2 errors: unchanged");
        toTick();
        S.advanceFine(300, 10);
        CHECK(Sim::fanPin() == 4 && climate.kickstartActive && !climate.currentHeatState, "3rd error: kickstart CH4, heater OFF");
        S.advance(6000);
        CHECK(ch() == 1, "then CH1 (got CH%d)", ch());
        S.advance(60000);
        CHECK(ch() == 1, "stays CH1 (got CH%d)", ch());
        Sim::dhtError(false);
        S.advance(12000);
        CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "sensor back at 17.0C: ColdLock again, fan OFF, heater ON");
        expectInvariants(S, "dht night coldlock");
    });

    tf::run("DHT error DAY during ColdLock: CH3, heater OFF", [] {
        bootInv(dayCfg(17.0f));
        S.advance(30000);
        Sim::dhtError(true);
        S.advance(40000);
        CHECK(ch() == 3 && !climate.currentHeatState, "CH3, heater OFF (CH%d heat=%d)", ch(), climate.currentHeatState);
        expectInvariants(S, "dht day coldlock");
    });

    tf::run("boost keeps priority during DHT error", [] {
        bootInv(dayCfg(22.0f));
        S.advance(20000);
        S.v0Boost(true);
        S.advance(15000);
        Sim::dhtError(true);
        S.advance(60000);
        CHECK(ch() == 4, "boost CH4 kept (got CH%d)", ch());
        S.v0Boost(false);
        S.advance(15000);
        CHECK(ch() == 3, "boost off during error -> DAY emergency CH3 (got CH%d)", ch());
        expectInvariants(S, "dht boost");
    });

    tf::run("DAY/NIGHT still switches during DHT error, NIGHT emergency CH1", [] {
        bootInv(dayCfg(22.0f));
        S.advance(20000);
        Sim::dhtError(true);
        S.advance(40000);
        CHECK(ch() == 3, "DAY emergency CH3");
        Sim::setLight(3800);
        S.advance(40000);
        CHECK(!climate.isDay && ch() == 1, "NIGHT detected, emergency CH1 (day=%d CH%d)", climate.isDay, ch());
        Sim::setLight(300);
        S.advance(40000);
        CHECK(climate.isDay && ch() == 3, "DAY again, CH3 (day=%d CH%d)", climate.isDay, ch());
        expectInvariants(S, "dht modes");
    });

    tf::run("light debounce DAY->NIGHT: 3 consecutive readings > 2500", [] {
        bootInv(dayCfg(22.0f));
        S.advance(15000);
        auto tickWith = [](int l) { Sim::setLight(l); toTick(); };
        tickWith(3000);
        tickWith(3000);
        CHECK(climate.isDay, "2 dark readings: still DAY");
        tickWith(3000);
        CHECK(!climate.isDay, "3rd dark reading: NIGHT");
    });

    tf::run("light debounce: a bright reading resets the counter; 2500 is not dark", [] {
        bootInv(dayCfg(22.0f));
        S.advance(15000);
        auto tickWith = [](int l) { Sim::setLight(l); toTick(); };
        tickWith(3000);
        tickWith(2000);
        tickWith(3000);
        tickWith(3000);
        CHECK(climate.isDay, "dark,bright,dark,dark: still DAY");
        tickWith(2500);
        tickWith(3000);
        tickWith(3000);
        CHECK(climate.isDay, "2500 resets the counter");
        tickWith(3000);
        CHECK(!climate.isDay, "3 in a row: NIGHT");
    });

    tf::run("light debounce NIGHT->DAY: 3 readings < 1500; 1500 is not bright", [] {
        bootInv(nightCfg(22.0f, 40.0f));
        S.advance(15000);
        auto tickWith = [](int l) { Sim::setLight(l); toTick(); };
        tickWith(1000);
        tickWith(1500);
        tickWith(1000);
        tickWith(1000);
        CHECK(!climate.isDay, "1500 resets the counter");
        tickWith(1000);
        CHECK(climate.isDay, "3 in a row: DAY");
    });

    tf::run("DAY->NIGHT from CH4: cycle reset, humidity logic takes over", [] {
        bootInv(dayCfg(28.0f, 40.0f));
        S.advance(90000);
        CHECK(ch() == 4 && climate.activeCycle == outHot, "day: CH4 + outHot");
        Sim::setLight(3800);
        S.advance(40000);
        CHECK(!climate.isDay, "NIGHT");
        CHECK(climate.activeCycle == outNormal && climate.autoOffset == 0.0f, "day cycle reset to outNormal/0.0");
        CHECK(climate.humCycle == humLow, "humCycle reset to humLow");
        S.advance(30000);
        CHECK(ch() == 1, "H=40 -> CH1 (got CH%d)", ch());
        expectInvariants(S, "day->night");
    });

    tf::run("NIGHT->DAY at 25.5C: day ladder from CH1", [] {
        bootInv(nightCfg(25.5f, 40.0f));
        S.advance(30000);
        CHECK(ch() == 1, "night CH1");
        Sim::setLight(300);
        S.advance(40000);
        CHECK(climate.isDay, "DAY");
        S.advance(60000);
        CHECK(ch() == 3, "CH3 at 25.5C (got CH%d)", ch());
        expectInvariants(S, "night->day");
    });

    tf::run("sunset while DAY ColdLock is active and T=17.7 (inside 17.5..19.5 band)", [] {
        Sim::Cfg c = dayCfg(17.0f, 60.0f);
        c.storage = true;
        bootInv(c);
        S.advance(30000);
        Sim::setT(17.7f);
        S.advance(30000);
        CHECK(climate.coldLockMode && ch() == 0, "DAY: still locked at 17.7C");
        Sim::setLight(3800);
        S.advance(40000);
        CHECK(!climate.isDay, "NIGHT");
        CHECK(climate.coldLockMode && ch() == 0, "ColdLock persists at night, fan OFF (CH%d)", ch());
        CHECK(climate.coldLockMode == climate.tooColdLock, "coldLockMode(%d) == tooColdLock(%d)", climate.coldLockMode, climate.tooColdLock);
        {
            const std::string& log = g_fs.files["/climate.log"];
            CHECK(log.find("MODE_CHANGE:DAY→NIGHT, ColdLock: fan OFF") != std::string::npos && log.find("DAY→NIGHT, CH1 start") == std::string::npos,
                  "log says 'DAY→NIGHT, ColdLock: fan OFF'");
        }
        Sim::setT(19.3f);
        S.advance(60000);
        CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "T=19.3: still locked, heater ON");
        Sim::setT(21.5f);
        S.advance(60000);
        CHECK(!climate.coldLockMode && ch() >= 1 && !climate.currentHeatState, "T=21.5: unlocked, fan CH%d, heater OFF", ch());
        Sim::setLight(300);
        S.advance(300000);
        CHECK(climate.isDay && !climate.currentHeatState && !climate.tooColdLock, "DAY at 21.5C: heater OFF, no stale ColdLock flag");
        CHECK(g_fs.files["/climate.log"].find("MODE_CHANGE:NIGHT→DAY, CH1 start") != std::string::npos, "log says 'NIGHT→DAY, CH1 start' without ColdLock");
        expectInvariants(S, "sunset in ColdLock");
    });

    tf::run("sunrise while NIGHT ColdLock is active (T=17.0): fan must stay OFF", [] {
        Sim::Cfg c = nightCfg(17.0f, 60.0f);
        c.storage = true;
        bootInv(c);
        S.advance(30000);
        CHECK(climate.coldLockMode && ch() == 0, "night: locked");
        double onFor = 0, maxOn = 0;
        int maxCh = 0;
        S.onStep = [&] {
            if (climate.coldLockMode && ch() != 0) { onFor += 0.1; maxOn = std::max(maxOn, onFor); maxCh = std::max(maxCh, ch()); }
            else onFor = 0;
        };
        Sim::setLight(300);
        S.advance(60000);
        S.onStep = nullptr;
        CHECK(climate.isDay, "DAY");
        CHECK(maxOn == 0, "fan ran %.1fs (up to CH%d) while ColdLock was active", maxOn, maxCh);
        CHECK(ch() == 0, "fan OFF at the end");
        const std::string& log = g_fs.files["/climate.log"];
        CHECK(log.find("MODE_CHANGE:NIGHT→DAY, ColdLock: fan OFF") != std::string::npos && log.find("NIGHT→DAY, CH1 start") == std::string::npos,
              "log says 'NIGHT→DAY, ColdLock: fan OFF'");
    });
}

static void userSuite() {
    tf::suite("CLIMATE: Blynk actions (V0/V10)");

    tf::run("V10 OFF/ON in DAY: all off; on restore cycle re-selected by temperature", [] {
        bootInv(dayCfg(25.7f));
        S.advance(90000);
        CHECK(ch() == 4 && climate.activeCycle == outHot, "running CH4 + outHot");
        S.v10System(false);
        S.advanceFine(10, 10);
        CHECK(ch() == 0 && Sim::fanPin() == 0 && !climate.currentHeatState, "OFF: fan and heater OFF immediately");
        CHECK(climate.activeCycle == outNormal && !climate.bootCycleSelected, "cycle reset, bootCycleSelected=false");
        S.advance(60000);
        CHECK(ch() == 0, "stays OFF");
        S.v10System(true);
        toTick();
        CHECK(climate.activeCycle == outHot, "restore: cycle by temperature = outHot (got %s)", cycleName(climate.activeCycle));
        S.advance(6000);
        CHECK(ch() >= 1 && climate.activeCycle == outHot, "landed on CH%d, still outHot", ch());
        S.advance(60000);
        CHECK(ch() == 4, "back to CH4 (got CH%d)", ch());
        expectInvariants(S, "V10");
    });

    tf::run("V10 OFF during kickstart: fan stays OFF", [] {
        bootInv(dayCfg(22.0f));
        S.advanceFine(1000, 10);
        CHECK(climate.kickstartActive, "kick in progress");
        S.v10System(false);
        S.advance(20000);
        CHECK(ch() == 0 && Sim::fanPin() == 0, "fan OFF after kick window");
        expectInvariants(S, "V10 during kick");
    });

    tf::run("V10 OFF/ON during ColdLock", [] {
        bootInv(dayCfg(17.0f));
        S.advance(30000);
        CHECK(climate.currentHeatState, "heater ON in ColdLock");
        S.v10System(false);
        S.advance(20000);
        CHECK(!climate.currentHeatState && ch() == 0, "OFF: heater OFF");
        S.v10System(true);
        S.advance(25000);
        CHECK(climate.coldLockMode && ch() == 0 && climate.currentHeatState, "ON: still locked, heater back ON");
        expectInvariants(S, "V10 coldlock");
    });

    for (float t0 : {19.5f, 20.0f, 20.5f}) {
        tf::run(tf::fmt("system ON (boot and V10) at T=%.1f: heater %s", t0, t0 <= 20.0f ? "ON" : "stays OFF"), [t0] {
            bool exp = t0 <= 20.0f;
            bootInv(dayCfg(t0));
            S.advanceFine(200, 10);
            CHECK(climate.currentHeatState == exp && Sim::heatPin() == exp, "boot: heater %s at the first tick", exp ? "ON" : "OFF");
            S.advance(30000);
            S.v10System(false);
            S.advance(30000);
            CHECK(!climate.currentHeatState, "V10 OFF: heater OFF");
            S.v10System(true);
            toTick();
            CHECK(climate.currentHeatState == exp && Sim::heatPin() == exp, "V10 ON: heater %s at the next tick", exp ? "ON" : "OFF");
            expectInvariants(S, "system on");
        });
    }

    tf::run("V0 ignored while system OFF", [] {
        bootInv(dayCfg(22.0f));
        S.advance(20000);
        S.v10System(false);
        S.v0Boost(true);
        S.advance(20000);
        CHECK(!climate.manualBoost && ch() == 0, "boost rejected");
    });

    tf::run("V0 boost DAY: CH4, then one step down per tick, never 0", [] {
        bootInv(dayCfg(22.0f));
        S.advance(30000);
        S.v0Boost(true);
        S.advance(1000);
        CHECK(ch() == 4, "CH4 within 1s");
        S.advance(60000);
        CHECK(ch() == 4, "holds CH4");
        S.v0Boost(false);
        std::string seq;
        for (int i = 0; i < 4; i++) {
            toTick();
            S.advance(300);
            seq += tf::fmt("CH%d ", ch());
        }
        CHECK(seq == "CH3 CH2 CH1 CH1 ", "after boost: %s(expected CH3 CH2 CH1 CH1)", seq.c_str());
        CHECK(S.inv.cycleChanges == 0, "cycle stays %s during boost and step-down (%ld changes)", cycleName(climate.activeCycle), S.inv.cycleChanges);
        expectInvariants(S, "V0 day");
    });

    tf::run("V0 boost NIGHT: CH4, off -> CH2 reset -> humidity zone", [] {
        bootInv(nightCfg(22.0f, 40.0f));
        S.advance(30000);
        S.v0Boost(true);
        S.advance(20000);
        CHECK(ch() == 4, "CH4 at night with boost");
        S.v0Boost(false);
        toTick();
        S.advance(300);
        CHECK(ch() == 2, "first tick after boost: CH2 (got CH%d)", ch());
        S.advance(10000);
        CHECK(ch() == 1, "then H=40 -> CH1 (got CH%d)", ch());
        expectInvariants(S, "V0 night");
    });

    tf::run("V0 boost during DAY ColdLock: CH4 + heater (thermostat), boost off -> fan OFF", [] {
        bootInv(dayCfg(17.0f));
        S.advance(30000);
        S.v0Boost(true);
        S.advance(30000);
        CHECK(ch() == 4 && climate.currentHeatState, "boost: CH4, heater ON (CH%d heat=%d)", ch(), climate.currentHeatState);
        S.v0Boost(false);
        S.advance(15000);
        CHECK(ch() == 0, "boost off -> ColdLock turns fan OFF again (CH%d)", ch());
    });
}

static void thermalSuite() {
    tf::suite("CLIMATE: thermal model (qualitative)");

    auto runModel = [](const char* label, bool day, double tout, double sun, float t0, double hours) {
        S.phy.enabled = true;
        S.phy.Tout = tout;
        S.phy.sun = sun;
        S.phy.Hout = 70;
        Sim::Cfg c = day ? dayCfg(t0, 65.0f) : nightCfg(t0, 65.0f);
        bootInv(c);
        double tmin = 99, tmax = -99;
        long both = 0, steps = 0;
        S.onStep = [&] {
            tmin = std::min(tmin, S.phy.T);
            tmax = std::max(tmax, S.phy.T);
            steps++;
            if (climate.currentHeatState && ch() > 0) both++;
        };
        S.advance((unsigned long)(600000));
        long c0 = S.inv.coldlockActivations, k0 = S.inv.kickstarts, h0 = S.inv.heatSwitches, s0 = S.inv.channelChanges;
        tmin = 99; tmax = -99; both = 0; steps = 0;
        S.advance((unsigned long)(hours * 3600000));
        S.onStep = nullptr;
        double per = 1.0 / hours;
        INFO("%s: T %.1f..%.1f C | ColdLock on %.1f/h | kickstarts %.1f/h | heater switches %.1f/h | fan switches %.1f/h | heater+fan together %.0f%% of time",
             label, tmin, tmax, (S.inv.coldlockActivations - c0) * per, (S.inv.kickstarts - k0) * per,
             (S.inv.heatSwitches - h0) * per, (S.inv.channelChanges - s0) * per, 100.0 * both / std::max(1L, steps));
        return (S.inv.kickstarts - k0) * per;
    };

    tf::run("cold day: outside +5C, weak sun", [runModel] {
        double k = runModel("cold day", true, 5.0, 0.3, 19.0f, 2.0);
        if (k > 6) OBSERVE("cold day model: still ~%.0f kickstarts/h with the 17.5..19.5 ColdLock band", k);
        expectInvariants(S, "cold day");
    });

    tf::run("cold night: outside +5C", [runModel] {
        runModel("cold night", false, 5.0, 0.0, 19.0f, 2.0);
        expectInvariants(S, "cold night");
    });

    tf::run("hot sunny day: outside +32C", [runModel] {
        runModel("hot day", true, 32.0, 1.0, 26.0f, 2.0);
        CHECK(ch() == 4 && climate.activeCycle == outHot, "CH4 + outHot (got CH%d + %s)", ch(), cycleName(climate.activeCycle));
        expectInvariants(S, "hot day");
    });

    tf::run("mild day with passing clouds", [] {
        S.phy.enabled = true;
        S.phy.Tout = 20;
        S.phy.Hout = 60;
        S.phy.noiseT = 0.05;
        bootInv(dayCfg(24.0f, 60.0f));
        std::mt19937 r(3);
        std::normal_distribution<double> n(0, 0.002);
        double cloud = 0.7;
        S.onStep = [&] {
            cloud = std::min(1.0, std::max(0.1, cloud + n(r)));
            S.phy.sun = cloud;
        };
        S.advance(600000);
        long s0 = S.inv.channelChanges, c0 = S.inv.cycleChanges;
        S.advance(4 * 3600000UL);
        S.onStep = nullptr;
        INFO("mild day: fan switches %.1f/h, cycle changes %.1f/h", (S.inv.channelChanges - s0) / 4.0, (S.inv.cycleChanges - c0) / 4.0);
        expectInvariants(S, "mild day");
    });
}

void suiteClimate() {
    unitSuite();
    daySuite();
    nightSuite();
    sensorSuite();
    userSuite();
    thermalSuite();
}
