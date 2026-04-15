#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

struct DataLogger {
    bool storageAvailable = false;
    unsigned long lastPeriodicLog = 0;
    unsigned long lastEventLog = 0;
    const unsigned long PERIODIC_INTERVAL = 120000;
    const unsigned long LOG_RETENTION_DAYS = 7;

    int ch1_activations = 0;
    int ch2_activations = 0;
    int ch3_activations = 0;
    int ch4_activations = 0;
    int overheat_events = 0;
    int dht_errors = 0;
    int coldlock_events = 0;

    int lastLoggedChannel = -1;
};

// === EXTERN ЗМІННІ ===
extern DataLogger logger;
extern bool timeInitialized;

// === ФУНКЦІЇ ===
void getTimestamp(char* buffer, size_t bufSize);
bool initSPIFFS();
void logToFile(const char* message);
void logPeriodicData();
void logFanChangeEvent(int oldCh, int newCh, float temp);
void logEvent(const char* eventType, const char* details = "");
void cleanOldLogs();

#endif // LOGGER_H
