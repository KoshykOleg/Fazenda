// logger.h
#ifndef LOGGER_H
#define LOGGER_H

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

extern DataLogger logger;

#endif // LOGGER_H