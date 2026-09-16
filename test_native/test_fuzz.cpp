#include "sim.h"
#include "tf.h"
#include <algorithm>
#include <cmath>

static Sim S;

static void runFuzz(int seed, int hours) {
    std::mt19937 r((unsigned)seed * 7919u + 13u);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(r); };
    auto N = [&](double s) { return std::normal_distribution<double>(0.0, s)(r); };
    auto chance = [&](double perHour) { return U(0, 1) < perHour / 36000.0; };

    double tMean = U(-5, 30), tAmp = U(3, 12), sunPeak = U(0.2, 1.0);
    const double noises[] = {0.0, 0.05, 0.1};
    const float hysts[] = {0.1f, 0.2f, 0.5f, 1.0f};
    const float humHyss[] = {2.0f, 5.0f, 8.0f};
    double startHour = U(0, 24);

    S.phy.enabled = true;
    S.phy.noiseT = noises[r() % 3];
    S.phy.noiseH = 0.5;
    S.phy.Hout = U(40, 85);
    S.phy.humSrc = U(0.005, 0.03);
    S.rng.seed((unsigned)seed);

    Sim::Cfg c;
    c.t = (float)(tMean + U(0, 8));
    c.h = (float)U(40, 80);
    c.setTemp = (float)U(18, 30);
    c.setHum = (float)U(40, 75);
    c.hyst = hysts[r() % 4];
    c.humHys = humHyss[r() % 3];
    c.storage = true;
    auto isDayAt = [](double tod) { return tod >= 6.0 && tod < 20.0; };
    c.light = isDayAt(startHour) ? 400 : 3600;
    S.phy.Tout = tMean;

    INFO("seed %d: Tout %.1f+-%.1f, sun %.2f, set %.1f/%.0f%%, hyst %.1f, humHys %.0f, noise %.2f, start %.1fh",
         seed, tMean, tAmp, sunPeak, c.setTemp, c.setHum, c.hyst, c.humHys, S.phy.noiseT, startHour);

    S.boot(c);
    S.inv.enabled = true;

    double walk = 0, cloud = 0.8;
    double boostUntil = -1, offUntil = -1, dhtUntil = -1, spikeUntil = -1;
    double tmin = 99, tmax = -99;
    unsigned long lastClean = 0;
    long glitches = 0, events = 0;
    const unsigned long total = (unsigned long)hours * 3600000UL;

    S.onStep = [&] {
        double now = simSeconds();
        double tod = std::fmod(startHour + now / 3600.0, 24.0);
        walk = std::max(-3.0, std::min(3.0, walk + N(0.002)));
        cloud = std::max(0.2, std::min(1.0, cloud + N(0.003)));
        S.phy.Tout = tMean + tAmp * std::sin(2 * M_PI * (tod - 9.0) / 24.0) + walk;
        bool day = isDayAt(tod);
        S.phy.sun = day ? sunPeak * std::sin(M_PI * (tod - 6.0) / 14.0) * cloud : 0.0;

        double light;
        if (std::fabs(tod - 6.0) < 0.5 || std::fabs(tod - 20.0) < 0.5) {
            double x = (tod < 12.0) ? (tod - 5.5) : (20.5 - tod);
            light = 3800 - 3400 * x + N(400);
        } else if (day) {
            light = 400 + (1 - cloud) * 800 + N(150);
        } else {
            light = 3600 + N(200);
        }
        if (now < spikeUntil) light = 2900;
        else if (day && chance(2)) spikeUntil = now + 10;
        Sim::setLight((int)std::max(0.0, std::min(4095.0, light)));

        if (chance(0.5)) { S.v5SetTemp((float)U(18, 30)); events++; }
        if (chance(0.25)) { S.v15Hyst((int)U(1, 20)); events++; }
        if (chance(0.25)) { S.v12HumHys((int)U(1, 10)); events++; }
        if (chance(0.25)) { S.v6Hum((float)U(35, 80)); events++; }
        if (boostUntil < 0 && chance(0.17)) { S.v0Boost(true); boostUntil = now + U(60, 1200); events++; }
        if (boostUntil > 0 && now > boostUntil) { S.v0Boost(false); boostUntil = -1; }
        if (offUntil < 0 && chance(0.08)) { S.v10System(false); offUntil = now + U(60, 1800); events++; }
        if (offUntil > 0 && now > offUntil) { S.v10System(true); offUntil = -1; }
        if (dhtUntil < 0 && chance(0.33)) { dhtUntil = now + U(5, 600); events++; }
        if (dhtUntil > 0 && now > dhtUntil) dhtUntil = -1;
        bool glitch = chance(1.0);
        if (glitch) glitches++;
        Sim::dhtError(dhtUntil > 0 || glitch);
        if (chance(0.5)) S.blynkConnected = !S.blynkConnected;

        logPeriodicData();
        if (millis() - lastClean > 3600000UL) { cleanOldLogs(); lastClean = millis(); }

        tmin = std::min(tmin, S.phy.T);
        tmax = std::max(tmax, S.phy.T);
    };

    S.advance(total);
    S.onStep = nullptr;

    double h = hours;
    INFO("seed %d: T %.1f..%.1f | ColdLock %ld | kicks %.1f/h | fan switches %.1f/h | heater switches %.1f/h | cycle changes %.1f/h | user events %ld | log %zu B | offscreen px %ld",
         seed, tmin, tmax, S.inv.coldlockActivations, S.inv.kickstarts / h, S.inv.channelChanges / h,
         S.inv.heatSwitches / h, S.inv.cycleChanges / h, events, g_fs.files["/climate.log"].size(), tft.oobPixels);

    for (int i = 1; i <= 12; i++) {
        CHECK(S.inv.viol[i] == 0, "%s: %ld%s%s", Invariants::name(i), S.inv.viol[i],
              S.inv.viol[i] ? " | e.g. " : "", S.inv.viol[i] ? S.inv.examples[i][0].c_str() : "");
        for (size_t k = 1; k < S.inv.examples[i].size(); k++) INFO("   %s", S.inv.examples[i][k].c_str());
    }
    CHECK(g_fs.files["/climate.log"].size() < 110000, "log size bounded by hourly rotation (%zu B)", g_fs.files["/climate.log"].size());
}

void suiteFuzz(int seeds, int hours, int firstSeed) {
    tf::suite(tf::fmt("FUZZ: %d seeds x %dh, random weather + random Blynk actions", seeds, hours));
    for (int s = firstSeed; s < firstSeed + seeds; s++) {
        tf::run(tf::fmt("seed %d", s), [s, hours] { runFuzz(s, hours); });
    }
}
