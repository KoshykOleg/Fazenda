// climate.cpp - Реалізація логіки клімат-контролю
#include "climate.h"
#include "display.h"
#include <DHT.h>

// === ЗОВНІШНІ ОБ'ЄКТИ (з main.cpp) ===
extern DHT dht;

// === СТРУКТУРА ЛОГЕРА (forward declaration в climate.h) ===
struct DataLogger {
    bool storageAvailable;
    int dht_errors;
    int coldlock_events;
    int overheat_events;
};

extern DataLogger logger;

// === ЗОВНІШНІ ФУНКЦІЇ (з main.cpp) ===
extern void logEvent(const char* eventType, const char* details);
extern void logFanChangeEvent(int oldCh, int newCh, float temp);

// === ПІНИ ===
#define RELAY_CH1_PIN 14
#define RELAY_CH2_PIN 27
#define RELAY_CH3_PIN 26
#define RELAY_CH4_PIN 32
#define RELAY_HEAT_PIN 19
#define LIGHT_SENSOR_PIN 34

// === КОНСТАНТИ ===
#define RELAY_SWITCH_DELAY 150
#define KICKSTART_DURATION 5000
#define CYCLE_CHANGE_DELAY 60000

#define COLDLOCK_TEMP_LOW 18.0
#define COLDLOCK_TEMP_HIGH 19.5
#define NIGHT_TEMP_OFFSET 4.0
#define NIGHT_TEMP_HYSTERESIS 0.5

// === ІНІЦІАЛІЗАЦІЯ ===
void climateInit(ClimateState* state) {
    state->set_temp_day = 25.0;
    state->set_hum_limit = 50.0;
    state->tempOffset = 0.0;
    state->hysteresis = 0.1;
    
    state->lastValidT = NAN;
    state->lastValidH = NAN;
    state->isDay = true;
    
    state->currentActiveChannel = 0;
    state->currentHeatState = false;
    state->tooColdLock = false;
    
    state->activeCycle = outNormal;
    state->autoOffset = 0.0;
    state->lastCycleChangeTime = 0;
    
    state->kickstartActive = false;
    state->kickstartTime = 0;
    state->targetChannelAfterKick = 0;
    
    state->pendingChannel = -1;
    state->lastFanSwitchTime = 0;
    
    state->systemOn = true;
    state->manualBoost = false;
    
    state->dhtRetryCount = 0;
    
    Serial.println("[CLIMATE] Module initialized");
}

// === ОБРОБКА ЧЕРГИ РЕЛЕ ===
void handleRelayQueue(ClimateState* state) {
    if (state->pendingChannel != -1 && 
        (millis() - state->lastFanSwitchTime >= RELAY_SWITCH_DELAY)) {
        
        digitalWrite(RELAY_CH1_PIN, HIGH);
        digitalWrite(RELAY_CH2_PIN, HIGH);
        digitalWrite(RELAY_CH3_PIN, HIGH);
        digitalWrite(RELAY_CH4_PIN, HIGH);
        
        if (state->pendingChannel == 1)      digitalWrite(RELAY_CH1_PIN, LOW);
        else if (state->pendingChannel == 2) digitalWrite(RELAY_CH2_PIN, LOW);
        else if (state->pendingChannel == 3) digitalWrite(RELAY_CH3_PIN, LOW);
        else if (state->pendingChannel == 4) digitalWrite(RELAY_CH4_PIN, LOW);
        
        state->currentActiveChannel = state->pendingChannel;
        state->pendingChannel = -1;
    }
}

// === ВСТАНОВЛЕННЯ КАНАЛУ ВЕНТИЛЯТОРА ===
void setFanChannel(ClimateState* state, int channel) {
    if (channel == 0) {
        state->kickstartActive = false;
        Serial.println("[setFanChannel] Turning OFF");
    }

    if (channel != 0 && state->currentActiveChannel == channel && 
        state->pendingChannel == -1) return;
    if (channel != 0 && state->pendingChannel == channel) return;

    digitalWrite(RELAY_CH1_PIN, HIGH);
    digitalWrite(RELAY_CH2_PIN, HIGH);
    digitalWrite(RELAY_CH3_PIN, HIGH);
    digitalWrite(RELAY_CH4_PIN, HIGH);
    
    if (channel == 0) {
        state->currentActiveChannel = 0;
        state->pendingChannel = -1;
        return;
    }

    state->pendingChannel = channel;
    state->lastFanSwitchTime = millis();
}

// === СТАРТ З КІКСТАРТОМ ===
void startFanWithKick(ClimateState* state, int targetChannel) {
    if (state->kickstartActive) {
        state->targetChannelAfterKick = targetChannel;
        return;
    }

    if (state->currentActiveChannel == 0 && targetChannel > 0) {
        setFanChannel(state, 4);
        state->kickstartActive = true;
        state->kickstartTime = millis();
        state->targetChannelAfterKick = targetChannel;
        Serial.printf("[KICK] CH4 → target CH%d\n", targetChannel);
    } 
    else {
        setFanChannel(state, targetChannel);
    }
}

// === КЕРУВАННЯ ОБІГРІВОМ ===
void heatControl(ClimateState* state, bool heat_state) {
    digitalWrite(RELAY_HEAT_PIN, heat_state ? LOW : HIGH);
    state->currentHeatState = heat_state;
}

// === ВИБІР ЦИКЛУ ПРИ СТАРТІ ===
void selectCycleOnBoot(ClimateState* state, float t) {
    if (t <= state->set_temp_day - 1.0) {
        state->activeCycle = outCold;
        state->autoOffset = 0.5;
        Serial.println("[BOOT] Cycle: outCold (+0.5)");
    } 
    else if (t <= state->set_temp_day + 0.5) {
        state->activeCycle = outNormal;
        state->autoOffset = 0.0;
        Serial.println("[BOOT] Cycle: outNormal (0.0)");
    } 
    else {
        state->activeCycle = outHot;
        state->autoOffset = -0.5;
        Serial.println("[BOOT] Cycle: outHot (-0.5)");
    }
    
    if (logger.storageAvailable) {
        char details[50];
        snprintf(details, sizeof(details), "T=%.1f, Cycle=%s", 
            t, 
            state->activeCycle == outCold ? "outCold" : 
            state->activeCycle == outNormal ? "outNormal" : "outHot");
        logEvent("BOOT_CYCLE", details);
    }
}

// === ПЕРЕМИКАННЯ ЦИКЛІВ ===
void checkCycleTransition(ClimateState* state, int newChannel) {
    if (!state->isDay) return;
    
    unsigned long now = millis();
    if (state->lastCycleChangeTime > 0 && 
        now - state->lastCycleChangeTime < CYCLE_CHANGE_DELAY) {
        return;
    }
    
    AutoCycle oldCycle = state->activeCycle;

    if (state->activeCycle == outNormal) {
        if (newChannel == 1) {
            state->activeCycle = outCold;
            state->autoOffset = 0.5;
        } else if (newChannel == 4) {
            state->activeCycle = outHot;
            state->autoOffset = -0.5;
        }
    }
    else if (state->activeCycle == outCold) {
        if (newChannel == 3) {
            state->activeCycle = outNormal;
            state->autoOffset = 0.0;
        }
    }
    else if (state->activeCycle == outHot) {
        if (newChannel == 2) {
            state->activeCycle = outNormal;
            state->autoOffset = 0.0;
        }
    }
    
    if (state->activeCycle != oldCycle) {
        const char* oldName = (oldCycle == outCold) ? "outCold" : 
                              (oldCycle == outNormal) ? "outNormal" : "outHot";
        const char* newName = (state->activeCycle == outCold) ? "outCold" : 
                              (state->activeCycle == outNormal) ? "outNormal" : "outHot";
        
        state->lastCycleChangeTime = now;
        
        Serial.printf("[CYCLE] %s → %s (CH%d, offset: %.1f)\n", 
            oldName, newName, newChannel, state->autoOffset);
        
        if (logger.storageAvailable) {
            char details[80];
            snprintf(details, sizeof(details), "%s→%s, CH%d, offset=%.1f", 
                oldName, newName, newChannel, state->autoOffset);
            logEvent("CYCLE_CHANGE", details);
        }
    }
}

// === ГОЛОВНА ЛОГІКА КЛІМАТ-КОНТРОЛЮ ===
void runClimateControl(ClimateState* state) {
    if (state->kickstartActive) return;

    Serial.printf(">>> runClimate: set_temp=%.2f offset=%.2f\n", 
        state->set_temp_day, state->tempOffset);

    unsigned long readStart = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int lightVal = analogRead(LIGHT_SENSOR_PIN);
    Serial.printf("DHT read: %lu ms\n", millis() - readStart);

    // === ОБРОБКА ПОМИЛКИ DHT ===
    if (isnan(h) || isnan(t)) {
        state->dhtRetryCount++;
        Serial.printf("[DHT ERROR] count: %d\n", state->dhtRetryCount);

        if (state->dhtRetryCount == 1) {
            logger.dht_errors++;
            logEvent("DHT_ERROR", "Sensor failed");
        }

        state->lastValidT = NAN;
        state->lastValidH = NAN;
        
        updateDisplayNew(NAN, NAN, state->currentActiveChannel, state->isDay, 
                        state->currentHeatState, state->tooColdLock, 
                        state->activeCycle, state->systemOn, false);
        
        if (state->systemOn) {
            if (state->currentActiveChannel == 0) {
                Serial.println("[DHT ERROR] OFF → Kick → CH3");
                startFanWithKick(state, 3);
            } 
            else if (state->currentActiveChannel != 3 && state->pendingChannel != 3) {
                Serial.printf("[DHT ERROR] CH%d → CH3\n", state->currentActiveChannel);
                setFanChannel(state, 3);
            }
            heatControl(state, false);
        }
        
        return;
    }

    // DHT OK
    if (state->dhtRetryCount > 0) {
        Serial.printf("[DHT OK] recovered after %d errors\n", state->dhtRetryCount);
    }
    state->dhtRetryCount = 0;
    state->lastValidT = t;
    state->lastValidH = h;

    // === ВИЗНАЧЕННЯ ДЕНЬ/НІЧ ===
    bool wasDay = state->isDay;

    if (state->isDay && lightVal > 2500) state->isDay = false;
    else if (!state->isDay && lightVal < 1500) state->isDay = true;

    if (wasDay != state->isDay) {
        if (state->isDay) {
            state->activeCycle = outNormal;
            state->autoOffset = 0.0;
            Serial.println("[MODE] NIGHT → DAY: Reset to outNormal");
            logEvent("MODE_CHANGE", "NIGHT→DAY, cycle=outNormal");
        } else {
            Serial.printf("[MODE] DAY → NIGHT: autoOffset %.1f cleared\n", state->autoOffset);
            logEvent("MODE_CHANGE", "DAY→NIGHT");
        }
    }

    // === СИСТЕМА ВИМКНЕНА ===
    if (!state->systemOn) {
        setFanChannel(state, 0);
        heatControl(state, false);
        return;
    }

    // === COLDLOCK ===
    if (t < COLDLOCK_TEMP_LOW && !state->tooColdLock) {
        state->tooColdLock = true;
        logger.coldlock_events++;
        logEvent("COLDLOCK", "Activated");
    }
    else if (t >= COLDLOCK_TEMP_HIGH && state->tooColdLock) {
        state->tooColdLock = false;
        logEvent("COLDLOCK", "Deactivated");
    }

    // === ОБІГРІВ ===
    bool nextHeatState = false;
    float nightTargetT = state->set_temp_day - NIGHT_TEMP_OFFSET;

    if (!state->isDay || state->tooColdLock) {
        if (t < (nightTargetT - NIGHT_TEMP_HYSTERESIS))      nextHeatState = true;
        else if (t >= nightTargetT)                          nextHeatState = false;
        else                                                 nextHeatState = state->currentHeatState;
    }

    // === ВИБІР КАНАЛУ ===
    int nextFanChannel = 0;
    
    if (state->tooColdLock) {
        nextFanChannel = 0;
        if (state->manualBoost) {
            state->manualBoost = false;
        }
    } 
    else if (state->manualBoost) {
        nextFanChannel = 4;
    }
    else if (state->isDay) {
        // === ДЕННА ЛОГІКА ===
        float finalOffset = state->tempOffset + state->autoOffset;
        float T1 = (state->set_temp_day - 1.0) + finalOffset;
        float T2 = (state->set_temp_day - 0.5) + finalOffset;
        float T3 = state->set_temp_day + finalOffset;
        float T4 = (state->set_temp_day + 0.5) + finalOffset;
        float T_OFF = state->set_temp_day - 1.0;

        Serial.printf("[DAY] T:%.1f | Cycle:%s | auto:%.1f | user:%.1f | CH:%d\n",
            t, 
            state->activeCycle == outCold ? "COLD" : 
            state->activeCycle == outNormal ? "NORM" : "HOT",
            state->autoOffset, state->tempOffset, state->currentActiveChannel);
        Serial.printf("[DAY] T1=%.1f T2=%.1f T3=%.1f T4=%.1f | OFF<%.1f\n",
            T1, T2, T3, T4, T_OFF);
        
        // STATE MACHINE
        if (state->currentActiveChannel == 0) {
            if (t < T_OFF) {
                nextFanChannel = 0;
            }
            else if (t >= state->set_temp_day) {
                if (state->activeCycle == outCold) {
                    nextFanChannel = 2;
                }
                else if (state->activeCycle == outNormal) {
                    nextFanChannel = (t >= state->set_temp_day + 0.6) ? 4 : 3;
                }
                else {
                    nextFanChannel = (t >= state->set_temp_day + 0.6) ? 4 : 3;
                }

                static bool bootCycleSelected = false;
                if (!bootCycleSelected) {
                    selectCycleOnBoot(state, t);
                    bootCycleSelected = true;
                }
            } else {
                nextFanChannel = 0;
            }
        }
        else if (state->currentActiveChannel == 1) {
            if (t < T_OFF) nextFanChannel = 0;
            else if (t >= (T2 + state->hysteresis)) nextFanChannel = 2;
            else nextFanChannel = 1;
        }
        else if (state->currentActiveChannel == 2) {
            if (t < T_OFF) nextFanChannel = 0;
            else if (t <= (T2 - state->hysteresis)) nextFanChannel = 1;
            else if (t >= (T3 + state->hysteresis)) nextFanChannel = 3;
            else nextFanChannel = 2;
        }
        else if (state->currentActiveChannel == 3) {
            if (t < T_OFF) nextFanChannel = 0;
            else if (t <= (T3 - state->hysteresis)) nextFanChannel = 2;
            else if (t >= (T4 + state->hysteresis)) nextFanChannel = 4;
            else nextFanChannel = 3;
        }
        else if (state->currentActiveChannel == 4) {
            if (t < T_OFF) nextFanChannel = 0;
            else if (t <= (T4 - state->hysteresis)) nextFanChannel = 3;
            else nextFanChannel = 4;
        }
        else {
            nextFanChannel = (t >= state->set_temp_day) ? 3 : 0;
            if (state->currentActiveChannel == 0 && t >= state->set_temp_day) {
                logger.overheat_events++;
                logEvent("OVERHEAT", "Started with high temp");
            }
        }
    }
    else {
        // === НІЧНА ЛОГІКА ===
        float T_OFF_NIGHT = nightTargetT - 1.0;
        
        Serial.printf("[NIGHT] T:%.1f H:%.1f | Lim:%.1f | CH:%d\n",
            t, h, state->set_hum_limit, state->currentActiveChannel);
        
        if (state->currentActiveChannel == 0) {
            if (t < T_OFF_NIGHT) nextFanChannel = 0;
            else if (h >= state->set_hum_limit) nextFanChannel = 2;
            else nextFanChannel = 1;
        }
        else if (state->currentActiveChannel == 1) {
            if (t < T_OFF_NIGHT) nextFanChannel = 0;
            else if (h >= (state->set_hum_limit + state->hysteresis)) nextFanChannel = 2;
            else nextFanChannel = 1;
        }
        else if (state->currentActiveChannel == 2) {
            if (t < T_OFF_NIGHT) nextFanChannel = 0;
            else if (h <= (state->set_hum_limit - state->hysteresis)) nextFanChannel = 1;
            else nextFanChannel = 2;
        }
        else {
            if (t < T_OFF_NIGHT) nextFanChannel = 0;
            else if (h >= state->set_hum_limit) nextFanChannel = 2;
            else nextFanChannel = 1;
            
            Serial.printf("[NIGHT] WARNING: Unexpected CH%d\n", state->currentActiveChannel);
        }
    }

    // === ЗАСТОСУВАННЯ ЗМІН ===
    if (nextHeatState != state->currentHeatState) {
        heatControl(state, nextHeatState);
    }

    if (!state->kickstartActive) {
        if (nextFanChannel == 0) {
            if (state->currentActiveChannel != 0) {
                Serial.printf("[FAN OFF] T=%.1f, was CH%d\n", t, state->currentActiveChannel);
                logFanChangeEvent(state->currentActiveChannel, 0, t);
                setFanChannel(state, 0);
                
                if (state->isDay) {
                    checkCycleTransition(state, 0);
                }
            }
        } else if (state->currentActiveChannel != nextFanChannel) {
            int oldCh = state->currentActiveChannel;
            startFanWithKick(state, nextFanChannel);
            logFanChangeEvent(oldCh, nextFanChannel, t);
            
            if (state->isDay) {
                checkCycleTransition(state, nextFanChannel);
            }
        }
    }

    // Лог
    if (state->isDay) {
        Serial.printf("[%.1f] T:%.1f(%.1f) H:%.1f | Fan:%d | Heat:%d | DAY | %s\n",
            millis()/1000.0, t, state->set_temp_day + state->tempOffset + state->autoOffset, 
            h, state->currentActiveChannel, state->currentHeatState,
            state->activeCycle == outCold ? "COLD" : 
            state->activeCycle == outNormal ? "NORM" : "HOT");
    } else {
        Serial.printf("[%.1f] T:%.1f(%.1f) H:%.1f | Fan:%d | Heat:%d | NIGHT\n",
            millis()/1000.0, t, state->set_temp_day + state->tempOffset, 
            h, state->currentActiveChannel, state->currentHeatState);
    }
}