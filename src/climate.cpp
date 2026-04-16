// climate.cpp
#include "climate.h"
#include "display.h"
#include <DHT.h>
#include "logger.h"

// === ЗОВНІШНІ ОБ'ЄКТИ (з main.cpp) ===
extern DHT dht;

// === КОНСТАНТИ ===
#define RELAY_SWITCH_DELAY 150
#define KICKSTART_DURATION 5000
#define CYCLE_CHANGE_DELAY 60000

//  ОНОВЛЕНІ КОНСТАНТИ
#define COLDLOCK_TEMP_LOW 17.5
#define COLDLOCK_EXIT_TEMP 18.0
#define NIGHT_TEMP_OFFSET 4.0
#define NIGHT_TEMP_CHECK_INTERVAL 60000
#define HUM_OFFSET 5.0

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
    
    // Денні цикли
    state->activeCycle = outNormal;
    state->autoOffset = 0.0;
    state->lastCycleChangeTime = 0;
    
    //  НІЧНА ЛОГІКА
    state->humCycle = humLow;
    state->humHys = 5.0;
    state->lastNightT = NAN;
    state->lastNightCheck = 0;
    state->coldLockMode = false;
    state->nightHumCtrlActive = false;
    
    // Kickstart
    state->kickstartActive = false;
    state->kickstartTime = 0;
    state->targetChannelAfterKick = 0;
    
    // Реле черга
    state->pendingChannel = -1;
    state->lastFanSwitchTime = 0;
    
    // Режими
    state->systemOn = true;
    state->manualBoost = false;
    
    // Діагностика
    state->dhtRetryCount = 0;
    state->bootCycleSelected = false;
    
    DBG_PRINTLN("[CLIMATE] Module initialized");
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
        DBG_PRINTLN("[setFanChannel] Turning OFF");
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
        DBG("[KICK] CH4 → target CH%d\n", targetChannel);
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
        DBG_PRINTLN("[BOOT] Cycle: outCold (+0.5)");
    } 
    else if (t <= state->set_temp_day + 0.5) {
        state->activeCycle = outNormal;
        state->autoOffset = 0.0;
        DBG_PRINTLN("[BOOT] Cycle: outNormal (0.0)");
    } 
    else {
        state->activeCycle = outHot;
        state->autoOffset = -0.5;
        DBG_PRINTLN("[BOOT] Cycle: outHot (-0.5)");
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
    if (newChannel == 0) return;
    
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
        
        DBG("[CYCLE] %s → %s (CH%d, offset: %.1f)\n", 
            oldName, newName, newChannel, state->autoOffset);
        
        if (logger.storageAvailable) {
            char details[80];
            snprintf(details, sizeof(details), "%s→%s, CH%d, offset=%.1f", 
                oldName, newName, newChannel, state->autoOffset);
            logEvent("CYCLE_CHANGE", details);
        }
    }
}

// НІЧНА ЛОГІКА
// === ПЕРЕВІРКА COLDLOCK РЕЖИМУ ===
void checkColdLockMode(ClimateState* state, float t) {
    if (!state->coldLockMode && t < (COLDLOCK_TEMP_LOW - state->hysteresis)) {
        state->coldLockMode = true;
        state->tooColdLock = true;  
        logger.coldlock_events++;
        logEvent("COLDLOCK", "Activated");
        
        DBG("[COLDLOCK] Activated at T=%.1f°C\n", t);
        
        if (state->currentActiveChannel == 0) {
            DBG_PRINTLN("[COLDLOCK] OFF → Kick → CH1");
            startFanWithKick(state, 1);
        } else if (state->currentActiveChannel != 1) {
            DBG("[COLDLOCK] CH%d → CH1\n", state->currentActiveChannel);
            setFanChannel(state, 1);
        }
    }
    
    else if (state->coldLockMode && t >= COLDLOCK_EXIT_TEMP) {
        state->coldLockMode = false;
        state->tooColdLock = false;
        logEvent("COLDLOCK", "Deactivated");
        
        DBG("[COLDLOCK] Deactivated at T=%.1f°C → Night humidity logic\n", t);
        
        
        state->lastNightT = NAN;
    }
}

// === ТЕМПЕРАТУРНА АДАПТАЦІЯ ВНОЧІ ===
void runNightTempAdaptation(ClimateState* state, float t) {
    unsigned long now = millis();
    
    if (isnan(state->lastNightT)) {
        state->lastNightT = t;
        state->lastNightCheck = now;
        DBG("[NIGHT_TEMP] Init: T=%.1f°C, CH%d\n", t, state->currentActiveChannel);
        return;
    }
    
    if (now - state->lastNightCheck < NIGHT_TEMP_CHECK_INTERVAL) {
        return;
    }

    float tempDelta = t - state->lastNightT;
    
    DBG("[NIGHT_TEMP] T:%.1f°C (was %.1f°C), delta: %+.1f°C, CH:%d\n",
        t, state->lastNightT, tempDelta, state->currentActiveChannel);
    
    int nextChannel = state->currentActiveChannel;
    
    if (tempDelta < 0) {
        DBG("[NIGHT_TEMP] T↓ → Keep CH%d\n", nextChannel);
    }
    else {
        if (state->currentActiveChannel == 1) {
            nextChannel = 2;
            DBG("[NIGHT_TEMP] T↑ → CH1→CH2\n");
        }
        else if (state->currentActiveChannel == 2) {
            nextChannel = 3;
            DBG("[NIGHT_TEMP] T↑ → CH2→CH3\n");
        }
        else if (state->currentActiveChannel == 3) {
            nextChannel = 3;
            DBG("[NIGHT_TEMP] T↑ → Stay at CH3 (max)\n");
        }
        else if (state->currentActiveChannel == 0) {
            nextChannel = 1;
            DBG("[NIGHT_TEMP] OFF → CH1\n");
        }
        else {
            nextChannel = 3;
            DBG("[NIGHT_TEMP] CH%d → CH3\n", state->currentActiveChannel);
        }
    }

    if (nextChannel != state->currentActiveChannel) {
        if (state->currentActiveChannel == 0) {
            startFanWithKick(state, nextChannel);
        } else {
            setFanChannel(state, nextChannel);
        }
        
        if (logger.storageAvailable) {
            char details[80];
            snprintf(details, sizeof(details), 
                "TempAdapt: %.1f→%.1f°C, CH%d→CH%d",
                state->lastNightT, t, state->currentActiveChannel, nextChannel);
            logEvent("NIGHT_TEMP_ADAPT", details);
        }
    }
    
    state->lastNightT = t;
    state->lastNightCheck = now;
}

// === ВОЛОГІСНИЙ КОНТРОЛЬ ВНОЧІ ===
void runNightHumidityControl(ClimateState* state, float t, float h) {

    float H_LOW = state->set_hum_limit - HUM_OFFSET_LOW;
    float H_MID = state->set_hum_limit;
    float H_HIGH = state->set_hum_limit + HUM_OFFSET_HIGH;
    
    DBG("[NIGHT_HUM] H:%.0f%% | Zones: CH1<%.0f%%, CH2=%.0f%%-%.0f%%, CH3>%.0f%% | Hys:%.0f%% | Cycle:%s\n",
        h, H_MID, H_MID, H_HIGH, H_HIGH, state->humHys,
        state->humCycle == humLow ? "humLow" : "humHigh");
    
    int nextChannel = state->currentActiveChannel;
    
    if (state->currentActiveChannel == 0) {
        nextChannel = (h >= H_MID) ? 2 : 1;
    }
    else if (state->currentActiveChannel == 1) {
        if (h >= (H_MID + state->humHys)) {
            nextChannel = 2;
            DBG("[NIGHT_HUM] CH1 → CH2 (H:%.0f%% ≥ %.0f%%)\n", h, H_MID + state->humHys);
        }
    }
    else if (state->currentActiveChannel == 2) {
        if (h <= (H_MID - state->humHys)) {
            nextChannel = 1;
            DBG("[NIGHT_HUM] CH2 → CH1 (H:%.0f%% ≤ %.0f%%)\n", h, H_MID - state->humHys);
        }
        else if (h >= (H_HIGH + state->humHys)) {
            nextChannel = 3;
            DBG("[NIGHT_HUM] CH2 → CH3 (H:%.0f%% ≥ %.0f%%)\n", h, H_HIGH + state->humHys);
        }
    }
    else if (state->currentActiveChannel == 3) {
        if (h <= (H_HIGH - state->humHys)) {
            nextChannel = 2;
            DBG("[NIGHT_HUM] CH3 → CH2 (H:%.0f%% ≤ %.0f%%)\n", h, H_HIGH - state->humHys);
        }
    }
    else {
        nextChannel = 2;
        DBG("[NIGHT_HUM] CH%d → CH2 (reset)\n", state->currentActiveChannel);
    }
    
    // Застосування
    if (nextChannel != state->currentActiveChannel) {
        if (state->currentActiveChannel == 0) {
            startFanWithKick(state, nextChannel);
        } else {
            setFanChannel(state, nextChannel);
        }
        checkHumCycleTransition(state, nextChannel);
        
        if (logger.storageAvailable) {
            char details[80];
            snprintf(details, sizeof(details), 
                "H=%.0f%%, CH%d→CH%d, %s",
                h, state->currentActiveChannel, nextChannel,
                state->humCycle == humLow ? "humLow" : "humHigh");
            logEvent("NIGHT_HUM_CTRL", details);
        }
    }
}

// === ПЕРЕМИКАННЯ ЦИКЛІВ ВОЛОГОСТІ ===
void checkHumCycleTransition(ClimateState* state, int newChannel) {
    HumCycle oldCycle = state->humCycle;
    
    if (state->humCycle == humLow && newChannel == 3) {
        state->humCycle = humHigh;
    }
    else if (state->humCycle == humHigh && newChannel == 1) {
        state->humCycle = humLow;
    }
    
    if (state->humCycle != oldCycle) {
        const char* oldName = (oldCycle == humLow) ? "humLow" : "humHigh";
        const char* newName = (state->humCycle == humLow) ? "humLow" : "humHigh";
        
        DBG("[HUM_CYCLE] %s → %s (CH%d)\n", oldName, newName, newChannel);
        
        if (logger.storageAvailable) {
            char details[60];
            snprintf(details, sizeof(details), "%s→%s, CH%d", 
                oldName, newName, newChannel);
            logEvent("HUM_CYCLE_CHANGE", details);
        }
    }
}

// === ГОЛОВНА ЛОГІКА КЛІМАТ-КОНТРОЛЮ ===
void runClimateControl(ClimateState* state) {
    if (state->kickstartActive) return;

    DBG(">>> runClimate: set_temp=%.2f offset=%.2f\n", 
        state->set_temp_day, state->tempOffset);

    unsigned long readStart = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int lightVal = analogRead(LIGHT_SENSOR_PIN);
    DBG("DHT read: %lu ms\n", millis() - readStart);

    // === ОБРОБКА ПОМИЛКИ DHT ===
    if (isnan(h) || isnan(t)) {
        if (state->dhtRetryCount < 100) state->dhtRetryCount++;
        DBG("[DHT ERROR] count: %d\n", state->dhtRetryCount);

        if (state->dhtRetryCount == 1) {
            logger.dht_errors++;
            logEvent("DHT_ERROR", "Sensor failed");
        }

        state->lastValidT = NAN;
        state->lastValidH = NAN;
        
        // ОНОВЛЕНИЙ ВИКЛИК
        updateDisplayNew(NAN, NAN, state->currentActiveChannel, state->isDay, 
                        state->currentHeatState, state->tooColdLock, 
                        state->activeCycle, state->humCycle,
                        state->systemOn, false);
        
        if (state->systemOn) {
            if (state->currentActiveChannel == 0) {
                DBG_PRINTLN("[DHT ERROR] OFF → Kick → CH3");
                startFanWithKick(state, 3);
            } 
            else if (state->currentActiveChannel != 3 && state->pendingChannel != 3) {
                DBG("[DHT ERROR] CH%d → CH3\n", state->currentActiveChannel);
                setFanChannel(state, 3);
            }
            heatControl(state, false);
        }
        
        return;
    }

    if (state->dhtRetryCount > 0) {
        DBG("[DHT OK] recovered after %d errors\n", state->dhtRetryCount);
    }
    state->dhtRetryCount = 0;
    state->lastValidT = t;
    state->lastValidH = h;

    bool wasDay = state->isDay;

    if (state->isDay && lightVal > 2500) state->isDay = false;
    else if (!state->isDay && lightVal < 1500) state->isDay = true;

    if (wasDay != state->isDay) {
        if (state->isDay) {
            state->activeCycle = outNormal;
            state->autoOffset = 0.0;

            if (state->currentActiveChannel == 0) {
                startFanWithKick(state, 1);
            } else if (state->currentActiveChannel != 1) {
                setFanChannel(state, 1);
            }
            
            DBG_PRINTLN("[MODE] NIGHT → DAY: CH1, wait for set_temp + hyst");
            logEvent("MODE_CHANGE", "NIGHT→DAY, CH1 standby");
        } else {
            DBG("[MODE] DAY → NIGHT: autoOffset %.1f cleared\n", state->autoOffset);
            state->autoOffset = 0.0;
            state->activeCycle = outNormal;
            state->bootCycleSelected = false;
            
            if (state->currentActiveChannel == 0) {
                startFanWithKick(state, 1);
            } else if (state->currentActiveChannel != 1) {
                setFanChannel(state, 1);
            }
            
            state->lastNightT = NAN;
            state->lastNightCheck = 0;
            state->coldLockMode = false;
            state->humCycle = humLow;
            state->nightHumCtrlActive = false;
            
            logEvent("MODE_CHANGE", "DAY→NIGHT, CH1 start");
        }
    }

    if (!state->systemOn) {
        setFanChannel(state, 0);
        heatControl(state, false);
        return;
    }

    bool nextHeatState = false;
    float nightTargetT = state->set_temp_day - NIGHT_TEMP_OFFSET;

    if (!state->isDay || state->tooColdLock) {
        if (t < (nightTargetT - state->hysteresis))      nextHeatState = true;
        else if (t >= nightTargetT)                       nextHeatState = false;
        else                                              nextHeatState = state->currentHeatState;
    }

    int nextFanChannel = 0;
    
    if (state->manualBoost) {
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

        DBG("[DAY] T:%.1f | Cycle:%s | auto:%.1f | user:%.1f | CH:%d\n",
            t, 
            state->activeCycle == outCold ? "COLD" : 
            state->activeCycle == outNormal ? "NORM" : "HOT",
            state->autoOffset, state->tempOffset, state->currentActiveChannel);
        DBG("[DAY] T1=%.1f T2=%.1f T3=%.1f T4=%.1f | OFF<%.1f\n",
            T1, T2, T3, T4, T_OFF);
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

                if (!state->bootCycleSelected) {
                    selectCycleOnBoot(state, t);
                    state->bootCycleSelected = true;
                    logger.overheat_events++;
                    logEvent("OVERHEAT", "Started with high temp");
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
    }
else {
    // === НІЧНА ЛОГІКА ===
    checkColdLockMode(state, t);
    
    if (state->coldLockMode) {

        nextFanChannel = state->currentActiveChannel;
        DBG("[NIGHT] ColdLock: T=%.1f°C, CH=%d, Heat=ON\n", t, nextFanChannel);
    }
    else if (state->nightHumCtrlActive) {
        runNightHumidityControl(state, t, h);
        nextFanChannel = state->currentActiveChannel;
    }
    else {
        float nightTargetT = state->set_temp_day - NIGHT_TEMP_OFFSET;
        float upperLimit = nightTargetT + 0.2;
        if (t > upperLimit) {
            DBG("[NIGHT_ADAPT] T=%.1f°C > %.1f°C (target=%.1f°C) → Cooling phase\n", 
                t, upperLimit, nightTargetT);
            
            runNightTempAdaptation(state, t);
            nextFanChannel = state->currentActiveChannel;
        }
        else {
            state->nightHumCtrlActive = true;
            
            DBG("[NIGHT_SWITCH] T=%.1f°C <= %.1f°C → HUM_CTRL active (permanent until sunrise)\n", 
                t, upperLimit);
            DBG("[NIGHT_SWITCH] Target reached: %.1f°C, switching from TEMP_ADAPT to HUM_CTRL\n", 
                nightTargetT);
            
            runNightHumidityControl(state, t, h);
            nextFanChannel = state->currentActiveChannel;
            
            if (logger.storageAvailable) {
                char details[80];
                snprintf(details, sizeof(details), 
                    "TEMP_ADAPT→HUM_CTRL, T=%.1f°C, target=%.1f°C", t, nightTargetT);
                logEvent("NIGHT_MODE_SWITCH", details);
            }
        }
    }
}

    // === ЗАСТОСУВАННЯ ЗМІН ===
    if (nextHeatState != state->currentHeatState) {
        heatControl(state, nextHeatState);
    }

    if (!state->kickstartActive) {
        if (nextFanChannel == 0) {
            if (state->currentActiveChannel != 0) {
                DBG("[FAN OFF] T=%.1f, was CH%d\n", t, state->currentActiveChannel);
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
        DBG("[%.1f] T:%.1f(%.1f) H:%.1f | Fan:%d | Heat:%d | DAY | %s\n",
            millis()/1000.0, t, state->set_temp_day + state->tempOffset + state->autoOffset, 
            h, state->currentActiveChannel, state->currentHeatState,
            state->activeCycle == outCold ? "COLD" : 
            state->activeCycle == outNormal ? "NORM" : "HOT");
    } else {
        const char* nightMode;
        if (state->coldLockMode) {
            nightMode = "COLDLOCK";
        } else if (state->nightHumCtrlActive) {
            nightMode = "HUM_CTRL";
        } else {
            nightMode = "TEMP_ADAPT";
        }
        
        DBG("[%.1f] T:%.1f(%.1f) H:%.1f | Fan:%d | Heat:%d | NIGHT | %s\n",
            millis()/1000.0, t, nightTargetT, 
            h, state->currentActiveChannel, state->currentHeatState,
            nightMode);
    }
}