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

static int lastEnd(int maxCh) { return getChannelX(maxCh, maxCh) + CHANNEL_WIDTH; }

void suiteDisplay() {
    tf::suite("DISPLAY: display.cpp on a fake 160x128 framebuffer");

    tf::run("getChannelX geometry", [] {
        CHECK(getChannelX(1, 4) == 45 && getChannelX(2, 4) == 63 && getChannelX(3, 4) == 81 && getChannelX(4, 4) == 99,
              "DAY x = 45,63,81,99");
        CHECK(getChannelX(1, 3) == 54 && getChannelX(2, 3) == 72 && getChannelX(3, 3) == 90, "NIGHT x = 54,72,90");
        CHECK(getChannelX(0, 4) == 0 && getChannelX(5, 4) == 0 && getChannelX(4, 3) == 0, "invalid channel -> 0");
        int l4 = 160 - lastEnd(4), l3 = 160 - lastEnd(3);
        CHECK(getChannelX(1, 4) == l4 && getChannelX(1, 3) == l3, "centered: left %d/%d = right %d/%d",
              getChannelX(1, 4), getChannelX(1, 3), l4, l3);
    });

    for (bool isDay : {true, false}) {
        tf::run(tf::fmt("indicators (new dynamic X), %s: inside screen, no overlap with channels", isDay ? "DAY" : "NIGHT"), [isDay] {
            int maxCh = isDay ? 4 : 3;
            tft.fillScreen(ST77XX_BLACK);
            drawChannels(0, maxCh);
            drawIndicators(true, true, 0, isDay);
            int y0 = INDICATOR_Y - INDICATOR_RADIUS, h = 2 * INDICATOR_RADIUS + 1;
            int cMin = 999, cMax = -1, oMin = 999, oMax = -1;
            for (int x = 0; x < 160; x++) {
                for (int y = y0; y < y0 + h; y++) {
                    if (tft.px(x, y) == C_CYAN) { cMin = std::min(cMin, x); cMax = std::max(cMax, x); }
                    if (tft.px(x, y) == C_ORANGE) { oMin = std::min(oMin, x); oMax = std::max(oMax, x); }
                }
            }
            CHECK(cMax >= 0 && oMax >= 0, "both indicators drawn (cold x %d..%d, heat x %d..%d)", cMin, cMax, oMin, oMax);
            CHECK(cMax < getChannelX(1, maxCh), "cold indicator left of CH1 (max x %d < %d)", cMax, getChannelX(1, maxCh));
            CHECK(oMin >= lastEnd(maxCh), "heat indicator right of last channel (min x %d >= %d)", oMin, lastEnd(maxCh));
            bool intact = true;
            for (int i = 1; i <= maxCh; i++)
                if (tft.count(getChannelX(i, maxCh), CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, C_DARK_GRAY) != CHANNEL_WIDTH * CHANNEL_HEIGHT)
                    intact = false;
            CHECK(intact, "channel rectangles untouched");
            CHECK(tft.oobPixels == 0, "no off-screen pixels");
            drawIndicators(false, false, 0, isDay);
            CHECK(tft.count(0, y0, 160, h, C_CYAN) == 0 && tft.count(0, y0, 160, h, C_ORANGE) == 0, "both erased when OFF");
        });
    }

    tf::run("DAY->NIGHT redraw: no stale indicator pixels", [] {
        tft.fillScreen(ST77XX_BLACK);
        updateDisplayNew(22, 50, 1, true, true, true, outNormal, humLow, true, false);
        g_fakeMillis += 1000;
        processChannelAnimation(true);
        updateDisplayNew(22, 50, 1, false, false, false, outNormal, humLow, true, false);
        int y0 = INDICATOR_Y - INDICATOR_RADIUS, h = 2 * INDICATOR_RADIUS + 1;
        long left = tft.count(0, y0, getChannelX(1, 3), h, C_CYAN) + tft.count(0, y0, getChannelX(1, 3), h, C_ORANGE);
        long right = tft.count(lastEnd(3), y0, 160 - lastEnd(3), h, C_CYAN) + tft.count(lastEnd(3), y0, 160 - lastEnd(3), h, C_ORANGE);
        CHECK(left == 0 && right == 0, "stale pixels: left %ld, right %ld", left, right);
        if (tft.oobPixels > 0)
            OBSERVE("mode-change clear rect draws %ld px below the screen (first at y=%d); harmless, TFT clips it", tft.oobPixels, tft.firstOobY);
    });

    tf::run("channel colors DAY/NIGHT", [] {
        drawChannels(2, 4);
        auto mid = [](int i, int m) { return tft.px(getChannelX(i, m) + 8, CHANNELS_BASE_Y + 7); };
        CHECK(mid(1, 4) == C_CYAN && mid(2, 4) == C_GREEN && mid(3, 4) == C_DARK_GRAY && mid(4, 4) == C_DARK_GRAY, "DAY CH2: cyan, green, gray, gray");
        tft.fillScreen(ST77XX_BLACK);
        drawChannels(3, 3);
        CHECK(mid(1, 3) == C_GREEN && mid(2, 3) == C_YELLOW && mid(3, 3) == C_RED, "NIGHT CH3: green, yellow, red");
    });

    tf::run("channel animation: 70ms per step, ends at target", [] {
        g_fakeMillis = 1000;
        startChannelAnimation(0, 4);
        unsigned long start = millis(), end = 0;
        while (channelAnim.active && millis() < start + 2000) {
            g_fakeMillis += 5;
            processChannelAnimation(true);
            if (!channelAnim.active) end = millis();
        }
        CHECK(channelAnim.currentStep == 4 && end - start == 280, "0->4 finished at CH4 in %lums (expected 280)", end - start);
        CHECK(Sim::displayedChannel(nullptr) == 4, "screen shows CH4");
        startChannelAnimation(4, 1);
        while (channelAnim.active) { g_fakeMillis += 5; processChannelAnimation(true); }
        CHECK(channelAnim.currentStep == 1 && Sim::displayedChannel(nullptr) == 1, "4->1 finished at CH1");
        startChannelAnimation(2, 2);
        CHECK(!channelAnim.active, "2->2 is a no-op");
    });

    tf::run("NaN temperature/humidity -> ERR; H=100 -> 3 digits", [] {
        updateDisplayNew(NAN, NAN, 0, true, false, false, outNormal, humLow, true, false);
        CHECK(tft.printedContains("T: ERR") && tft.printedContains("H: ERR"), "ERR texts printed");
        updateDisplayNew(22.5f, 100.0f, 0, true, false, false, outNormal, humLow, true, false);
        CHECK(tft.printedContains("T:22.5") && tft.printedContains("H:100%"), "T:22.5 and H:100%%");
    });

    tf::run("Blynk status dot", [] {
        updateDisplayNew(22, 50, 0, true, false, false, outNormal, humLow, true, true);
        CHECK(tft.px(110, 5) == C_GREEN, "connected -> green");
        updateDisplayNew(22, 50, 0, true, false, false, outNormal, humLow, true, false);
        CHECK(tft.px(110, 5) == C_RED, "disconnected -> red");
    });

    tf::run("periodic 10-min redraw restores a white screen", [] {
        S.boot(dayCfg(22.0f));
        S.advance(60000);
        tft.fillScreen(0xFFFF);
        S.advance(600000);
        bool day = false;
        int shown = Sim::displayedChannel(&day);
        CHECK(tft.initCount >= 2, "tft.initR() called again (%ld)", tft.initCount);
        CHECK(tft.count(0, 0, 160, 128, 0xFFFF) == 0, "no white pixels left");
        CHECK(day && shown == climate.currentActiveChannel, "screen DAY/CH%d == real CH%d", shown, climate.currentActiveChannel);
    });

    tf::run("full run with display invariant: 3h DAY/NIGHT with changes", [] {
        S.boot(dayCfg(24.0f, 60.0f));
        S.inv.enabled = true;
        const float temps[] = {24.0f, 25.2f, 26.5f, 25.0f, 22.0f, 17.0f, 18.5f, 23.0f};
        for (int round = 0; round < 3; round++) {
            for (float t : temps) {
                Sim::setT(t);
                S.advance(300000);
            }
            Sim::setLight(round % 2 == 0 ? 3800 : 300);
        }
        expectInvariants(S, "display run");
    });

    tf::run("channel change while animation is running -> screen desync", [] {
        S.boot(dayCfg(22.0f));
        S.advance(60000);
        S.advanceFine(S.msToNextTick() + 10, 10);
        S.advanceFine(S.msToNextTick() - 200, 10);
        CHECK(S.msToNextTick() == 200, "aligned 200ms before the tick");
        S.v0Boost(true);
        Sim::dhtError(true);
        S.advanceFine(2000, 10);
        int shown = Sim::displayedChannel(nullptr);
        CHECK(shown == climate.currentActiveChannel, "after 2s: screen CH%d, real CH%d", shown, climate.currentActiveChannel);
        S.advanceFine(60000, 50);
        shown = Sim::displayedChannel(nullptr);
        CHECK(shown == climate.currentActiveChannel, "after 60s: screen CH%d, real CH%d", shown, climate.currentActiveChannel);
    });

    tf::run("SYSTEM OFF pressed while channel animation runs", [] {
        S.boot(dayCfg(22.0f));
        S.advance(60000);
        S.v0Boost(true);
        S.advanceFine(200, 10);
        CHECK(channelAnim.active, "animation running");
        S.v10System(false);
        S.advanceFine(1000, 10);
        CHECK(tft.printedContains("SYSTEM OFF"), "'SYSTEM OFF' printed");
        long painted = 0;
        for (int i = 1; i <= 4; i++)
            painted += CHANNEL_WIDTH * CHANNEL_HEIGHT - tft.count(getChannelX(i, 4), CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, ST77XX_BLACK);
        CHECK(painted == 0, "channel bars must not be drawn over 'SYSTEM OFF' (%ld px painted)", painted);
    });

    tf::run("animation retargets when channel changes mid-way", [] {
        g_fakeMillis = 1000;
        updateDisplayNew(22, 50, 1, true, false, false, outNormal, humLow, true, false);
        for (int i = 0; i < 40; i++) { g_fakeMillis += 5; processChannelAnimation(true); }
        CHECK(!channelAnim.active && Sim::displayedChannel(nullptr) == 1, "settled at CH1");
        updateDisplayNew(22, 50, 4, true, false, false, outNormal, humLow, true, false);
        g_fakeMillis += 75;
        processChannelAnimation(true);
        CHECK(channelAnim.active && channelAnim.currentStep == 2, "mid-way at step 2");
        updateDisplayNew(22, 50, 1, true, false, false, outNormal, humLow, true, false);
        for (int i = 0; i < 100; i++) { g_fakeMillis += 5; processChannelAnimation(true); }
        CHECK(!channelAnim.active && Sim::displayedChannel(nullptr) == 1, "retargeted down, ends at CH1 (screen CH%d)", Sim::displayedChannel(nullptr));
        updateDisplayNew(22, 50, 3, true, false, false, outNormal, humLow, true, false);
        g_fakeMillis += 75;
        processChannelAnimation(true);
        updateDisplayNew(22, 50, 2, true, false, false, outNormal, humLow, true, false);
        for (int i = 0; i < 100; i++) { g_fakeMillis += 5; processChannelAnimation(true); }
        CHECK(!channelAnim.active && Sim::displayedChannel(nullptr) == 2, "new target equals current step: stops at CH2 (screen CH%d)", Sim::displayedChannel(nullptr));
    });

    tf::run("DAY/NIGHT switch while SYSTEM OFF keeps the OFF screen", [] {
        S.boot(dayCfg(22.0f, 40.0f));
        S.advance(60000);
        S.v10System(false);
        S.advance(5000);
        size_t before = 0;
        for (const auto& p : tft.printed) if (p == "SYSTEM OFF") before++;
        Sim::setLight(3800);
        S.advance(60000);
        CHECK(!climate.isDay, "switched to NIGHT");
        size_t after = 0;
        for (const auto& p : tft.printed) if (p == "SYSTEM OFF") after++;
        CHECK(after > before, "'SYSTEM OFF' printed again after the mode change");
        long painted = 0;
        for (int i = 1; i <= 3; i++)
            painted += CHANNEL_WIDTH * CHANNEL_HEIGHT - tft.count(getChannelX(i, 3), CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, ST77XX_BLACK);
        CHECK(painted == 0, "no channel bars under 'SYSTEM OFF' (%ld px)", painted);
        S.v10System(true);
        S.advance(60000);
        bool day = true;
        int shown = Sim::displayedChannel(&day);
        CHECK(!day && shown == climate.currentActiveChannel, "after ON: NIGHT layout, CH%d == real CH%d", shown, climate.currentActiveChannel);
    });

    tf::run("periodic redraw while SYSTEM OFF keeps the OFF screen", [] {
        S.boot(dayCfg(22.0f));
        S.advance(30000);
        S.v10System(false);
        S.advance(700000);
        CHECK(tft.initCount >= 2, "periodic redraw happened");
        long painted = 0;
        for (int i = 1; i <= 4; i++)
            painted += CHANNEL_WIDTH * CHANNEL_HEIGHT - tft.count(getChannelX(i, 4), CHANNELS_BASE_Y, CHANNEL_WIDTH, CHANNEL_HEIGHT, ST77XX_BLACK);
        CHECK(painted == 0 && tft.printed.back() == "SYSTEM OFF", "OFF screen intact (%ld px, last text '%s')", painted, tft.printed.back().c_str());
    });
}
