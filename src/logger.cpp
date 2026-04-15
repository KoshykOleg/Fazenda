#include "logger.h"
#include "climate.h"
#include <SPIFFS.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <Arduino.h>

// === EXTERN ЗАЛЕЖНОСТІ ===
extern ClimateState climate;

// === ГЛОБАЛЬНІ ЗМІННІ ===
DataLogger logger;
bool timeInitialized = false;

// === NTP КОНФІГУРАЦІЯ ===
const char* ntpServer = "pool.ntp.org";
extern const long gmtOffset_sec = 2 * 3600;
extern const int daylightOffset_sec = 0;

// === TIMESTAMP (char buffer замість String) ===
void getTimestamp(char* buffer, size_t bufSize) {
    struct tm timeinfo;
    if (timeInitialized && getLocalTime(&timeinfo)) {
        strftime(buffer, bufSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
        return;
    }
    unsigned long uptime = millis() / 1000;
    snprintf(buffer, bufSize, "[%lus]", uptime);
}

// === SPIFFS ІНІЦІАЛІЗАЦІЯ ===
bool initSPIFFS() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] Mount failed!");
        return false;
    }

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();

    Serial.printf("[SPIFFS] Total: %u, Used: %u, Free: %u bytes\n",
        totalBytes, usedBytes, totalBytes - usedBytes);

    if (!SPIFFS.exists("/climate.log")) {
        File file = SPIFFS.open("/climate.log", FILE_WRITE);
        if (file) {
            file.println("=== FAZENDA CLIMATE LOG ===");
            file.println("Timestamp,Temp,Hum,Fan,Heat,Mode,Event");
            file.close();
            Serial.println("[SPIFFS] Log file created");
        }
    }

    return true;
}

// === ЛОГУВАННЯ В ФАЙЛ ===
void logToFile(const char* message) {
    if (!logger.storageAvailable) return;

    File file = SPIFFS.open("/climate.log", FILE_APPEND);
    if (!file) {
        Serial.println("[SPIFFS] Failed to open log file");
        return;
    }

    char timestamp[32];
    getTimestamp(timestamp, sizeof(timestamp));
    file.printf("%s %s\n", timestamp, message);
    file.close();
}

// === ПЕРІОДИЧНЕ ЛОГУВАННЯ ===
void logPeriodicData() {
    if (!logger.storageAvailable) return;

    unsigned long now = millis();
    if (now - logger.lastPeriodicLog < logger.PERIODIC_INTERVAL) return;

    logger.lastPeriodicLog = now;

    char logLine[256];

    if (climate.isDay) {
        const char* cycleName = (climate.activeCycle == outCold) ? "COLD" :
                                (climate.activeCycle == outNormal) ? "NORM" : "HOT";
        snprintf(logLine, sizeof(logLine),
            "%lu,%.1f,%.1f,%d,%d,DAY,%s,autoOff=%.1f,PERIODIC",
            now / 1000,
            climate.lastValidT,
            climate.lastValidH,
            climate.currentActiveChannel,
            climate.currentHeatState ? 1 : 0,
            cycleName,
            climate.autoOffset
        );
    } else {
        snprintf(logLine, sizeof(logLine),
            "%lu,%.1f,%.1f,%d,%d,NIGHT,PERIODIC",
            now / 1000,
            climate.lastValidT,
            climate.lastValidH,
            climate.currentActiveChannel,
            climate.currentHeatState ? 1 : 0
        );
    }

    logToFile(logLine);
}

// === ЛОГУВАННЯ ЗМІНИ ВЕНТИЛЯТОРА ===
void logFanChangeEvent(int oldChannel, int newChannel, float temp) {
    if (!logger.storageAvailable) return;

    if (newChannel == 1) logger.ch1_activations++;
    else if (newChannel == 2) logger.ch2_activations++;
    else if (newChannel == 3) logger.ch3_activations++;
    else if (newChannel == 4) logger.ch4_activations++;

    char logLine[200];
    snprintf(logLine, sizeof(logLine),
        "%lu,%.1f,%.1f,%d,%d,%s,FAN_CHANGE:CH%d->CH%d",
        millis() / 1000,
        temp,
        climate.lastValidH,
        newChannel,
        climate.currentHeatState ? 1 : 0,
        climate.isDay ? "DAY" : "NIGHT",
        oldChannel,
        newChannel
    );

    logToFile(logLine);

    Serial.printf("[LOGGER] Fan: CH%d \xe2\x86\x92 CH%d (Stats: CH1=%d CH2=%d CH3=%d CH4=%d)\n",
        oldChannel, newChannel,
        logger.ch1_activations, logger.ch2_activations,
        logger.ch3_activations, logger.ch4_activations);
}

// === ЛОГУВАННЯ ПОДІЙ ===
void logEvent(const char* eventType, const char* details) {
    if (!logger.storageAvailable) return;

    char logLine[200];
    snprintf(logLine, sizeof(logLine),
        "%lu,%.1f,%.1f,%d,%d,%s,%s%s%s",
        millis() / 1000,
        climate.lastValidT,
        climate.lastValidH,
        climate.currentActiveChannel,
        climate.currentHeatState ? 1 : 0,
        climate.isDay ? "DAY" : "NIGHT",
        eventType,
        strlen(details) > 0 ? ":" : "",
        details
    );

    logToFile(logLine);
}

// === ОЧИЩЕННЯ СТАРИХ ЛОГІВ ===
void cleanOldLogs() {
    if (!logger.storageAvailable) return;

    File file = SPIFFS.open("/climate.log", FILE_READ);
    if (!file) return;

    size_t fileSize = file.size();
    file.close();

    if (fileSize > 100000) {
        Serial.println("[SPIFFS] Log file too large, rotating...");

        File src = SPIFFS.open("/climate.log", FILE_READ);
        if (!src) return;
        src.seek(fileSize - 50000);

        File dst = SPIFFS.open("/climate.tmp", FILE_WRITE);
        if (!dst) { src.close(); return; }

        dst.println("=== LOG ROTATED ===");

        uint8_t buf[256];
        while (src.available()) {
            esp_task_wdt_reset();
            size_t bytesRead = src.read(buf, sizeof(buf));
            dst.write(buf, bytesRead);
        }
        src.close();
        dst.close();

        SPIFFS.remove("/climate.log");
        SPIFFS.rename("/climate.tmp", "/climate.log");

        Serial.println("[SPIFFS] Log rotation complete");
    }
}
