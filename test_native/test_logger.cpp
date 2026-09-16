#include "sim.h"
#include "tf.h"

static void freshFs() {
    g_fs = FakeFsState();
    g_timeValid = false;
    timeInitialized = false;
    logger.storageAvailable = false;
    logger.loggingEnabled = true;
    logger.lastPeriodicLog = 0;
}

static std::string lastLine() {
    const std::string& d = g_fs.files["/climate.log"];
    if (d.empty()) return "";
    size_t end = d.size() - 1;
    size_t start = d.rfind('\n', end - 1);
    return d.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1));
}

static void setClimate() {
    climateInit(&climate);
    climate.lastValidT = 25.5f;
    climate.lastValidH = 53.0f;
    climate.currentActiveChannel = 2;
    climate.isDay = true;
}

void suiteLogger() {
    tf::suite("LOGGER: logger.cpp on a fake SPIFFS");

    tf::run("initSPIFFS: mount failure, header creation, existing file kept", [] {
        freshFs();
        g_fs.mountOk = false;
        CHECK(!initSPIFFS(), "mount failure -> false");
        freshFs();
        CHECK(initSPIFFS(), "mount ok -> true");
        CHECK(g_fs.files["/climate.log"] == "=== FAZENDA CLIMATE LOG ===\r\nTimestamp,Temp,Hum,Fan,Heat,Mode,Event\r\n", "header written");
        g_fs.files["/climate.log"] = "old data\n";
        initSPIFFS();
        CHECK(g_fs.files["/climate.log"] == "old data\n", "existing log not overwritten");
    });

    tf::run("logEvent line format", [] {
        freshFs();
        initSPIFFS();
        logger.storageAvailable = true;
        setClimate();
        g_fakeMillis = 12345;
        logEvent("TEST", "abc");
        CHECK(lastLine() == "[12s] 12,25.5,53.0,2,0,DAY,TEST:abc", "no NTP: '%s'", lastLine().c_str());
        logEvent("EMPTY");
        CHECK(lastLine() == "[12s] 12,25.5,53.0,2,0,DAY,EMPTY", "empty details -> no colon: '%s'", lastLine().c_str());
        g_timeValid = true;
        timeInitialized = true;
        logEvent("NTP", "x");
        std::string l = lastLine();
        bool ok = l.size() > 20 && l[4] == '-' && l[7] == '-' && l[10] == ' ' && l[13] == ':' && l[16] == ':';
        CHECK(ok, "with NTP: 'YYYY-MM-DD HH:MM:SS ...' -> '%s'", l.c_str());
        climate.isDay = false;
        climate.currentHeatState = true;
        logEvent("N", "y");
        CHECK(lastLine().find(",1,NIGHT,N:y") != std::string::npos, "NIGHT + heat flag: '%s'", lastLine().c_str());
    });

    tf::run("logging disabled / storage unavailable: no writes, counters still count", [] {
        freshFs();
        initSPIFFS();
        setClimate();
        logger.storageAvailable = true;
        logger.loggingEnabled = false;
        std::string before = g_fs.files["/climate.log"];
        logEvent("X", "y");
        logFanChangeEvent(1, 2, 25.0f);
        CHECK(g_fs.files["/climate.log"] == before, "V16 OFF: nothing written");
        CHECK(logger.ch2_activations == 1, "V16 OFF: CH2 counter incremented");
        logger.loggingEnabled = true;
        logger.storageAvailable = false;
        logFanChangeEvent(2, 3, 25.0f);
        logPeriodicData();
        CHECK(g_fs.files["/climate.log"] == before, "no SPIFFS: nothing written");
        CHECK(logger.ch3_activations == 1, "no SPIFFS: CH3 counter incremented");
    });

    tf::run("logFanChangeEvent: counters and line", [] {
        freshFs();
        initSPIFFS();
        logger.storageAvailable = true;
        setClimate();
        g_fakeMillis = 50000;
        logFanChangeEvent(0, 3, 23.04f);
        CHECK(lastLine() == "[50s] 50,23.0,53.0,3,0,DAY,FAN_CHANGE:CH0->CH3", "line: '%s'", lastLine().c_str());
        logFanChangeEvent(3, 0, 23.0f);
        logFanChangeEvent(0, 1, 23.0f);
        logFanChangeEvent(1, 4, 23.0f);
        CHECK(logger.ch1_activations == 1 && logger.ch2_activations == 0 && logger.ch3_activations == 1 && logger.ch4_activations == 1,
              "counters 1/0/1/1, CH0 not counted");
    });

    tf::run("logPeriodicData: every 120s, DAY and NIGHT format", [] {
        freshFs();
        initSPIFFS();
        logger.storageAvailable = true;
        setClimate();
        g_fakeMillis = 0;
        int calls = 0;
        while (g_fakeMillis < 600000) {
            g_fakeMillis += 100;
            logPeriodicData();
            calls++;
        }
        const std::string& d = g_fs.files["/climate.log"];
        size_t n = 0;
        for (size_t p = d.find("PERIODIC"); p != std::string::npos; p = d.find("PERIODIC", p + 1)) n++;
        CHECK(n == 5, "10 min -> 5 entries (got %zu)", n);
        CHECK(lastLine().find(",DAY,NORM,autoOff=0.0,PERIODIC") != std::string::npos, "DAY format: '%s'", lastLine().c_str());
        climate.isDay = false;
        g_fakeMillis += 120000;
        logPeriodicData();
        CHECK(lastLine().find(",NIGHT,PERIODIC") != std::string::npos, "NIGHT format: '%s'", lastLine().c_str());
    });

    tf::run("cleanOldLogs: >100KB -> last 50KB + header", [] {
        freshFs();
        logger.storageAvailable = true;
        std::string orig;
        int i = 0;
        while (orig.size() < 110000) {
            char line[80];
            snprintf(line, sizeof(line), "[%ds] %d,25.0,50.0,1,0,DAY,EVENT:%d\n", i, i, i * 7);
            orig += line;
            i++;
        }
        g_fs.files["/climate.log"] = orig;
        cleanOldLogs();
        const std::string& d = g_fs.files["/climate.log"];
        const std::string hdr = "=== LOG ROTATED ===\r\n";
        size_t cut = orig.size() - 50000;
        CHECK(orig[cut - 1] != '\n', "test data: cut lands mid-line");
        std::string tail = orig.substr(orig.find('\n', cut - 1) + 1);
        CHECK(d.compare(0, hdr.size(), hdr) == 0, "starts with rotation header");
        CHECK(d.substr(hdr.size()) == tail, "keeps the tail from the first complete line (%zu B, expected %zu B)", d.size() - hdr.size(), tail.size());
        CHECK(d.size() <= 50000 + hdr.size() && d.size() > 50000 - 80, "size %zu within 50KB + header", d.size());
        CHECK(!g_fs.files.count("/climate.tmp"), "temp file removed");
        std::string first = d.substr(hdr.size(), d.find('\n', hdr.size()) - hdr.size());
        CHECK(first.size() > 2 && first[0] == '[', "first line after rotation is complete: '%s'", first.c_str());
    });

    tf::run("cleanOldLogs: cut exactly on a line boundary keeps that line", [] {
        freshFs();
        logger.storageAvailable = true;
        std::string line = std::string(49, 'x') + "\n";
        std::string orig;
        while (orig.size() < 110000) orig += line;
        g_fs.files["/climate.log"] = orig;
        size_t cut = orig.size() - 50000;
        CHECK(orig[cut - 1] == '\n', "test data: cut lands on a boundary");
        cleanOldLogs();
        const std::string& d = g_fs.files["/climate.log"];
        const std::string hdr = "=== LOG ROTATED ===\r\n";
        CHECK(d.substr(hdr.size()) == orig.substr(cut), "exactly the last 50000 B kept, no line dropped");
    });

    tf::run("cleanOldLogs: no newline in the tail -> header only, no crash", [] {
        freshFs();
        logger.storageAvailable = true;
        g_fs.files["/climate.log"] = std::string(100001, 'a');
        cleanOldLogs();
        CHECK(g_fs.files["/climate.log"] == "=== LOG ROTATED ===\r\n", "only header left (%zu B)", g_fs.files["/climate.log"].size());
    });

    tf::run("cleanOldLogs boundary: 100000 bytes kept, 100001 rotated", [] {
        freshFs();
        logger.storageAvailable = true;
        g_fs.files["/climate.log"] = std::string(100000, 'a');
        cleanOldLogs();
        CHECK(g_fs.files["/climate.log"].size() == 100000, "100000 untouched");
        g_fs.files["/climate.log"] = std::string(100000, 'a') + "\n";
        cleanOldLogs();
        CHECK(g_fs.files["/climate.log"].size() < 100000, "100001 rotated");
    });

    tf::run("open failures do not crash", [] {
        freshFs();
        initSPIFFS();
        logger.storageAvailable = true;
        setClimate();
        g_fs.failOpen = true;
        logEvent("X", "y");
        logFanChangeEvent(0, 1, 20.0f);
        logPeriodicData();
        cleanOldLogs();
        g_fs.failOpen = false;
        g_fs.files.clear();
        g_fs.failOpen = true;
        CHECK(initSPIFFS(), "initSPIFFS returns true even if header cannot be created");
        CHECK(true, "no crash");
    });

    tf::run("very long details are truncated safely", [] {
        freshFs();
        initSPIFFS();
        logger.storageAvailable = true;
        setClimate();
        std::string big(500, 'Z');
        logEvent("LONG", big.c_str());
        std::string l = lastLine();
        CHECK(l.size() < 240, "line length %zu (<240)", l.size());
    });
}
