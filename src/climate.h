// climate.h - Модуль керування кліматом
#ifndef CLIMATE_H
#define CLIMATE_H

#include <Arduino.h>
#include "config.h"

// === FORWARD DECLARATIONS ===
struct DataLogger;  // ← ДОДАНО

// === СТРУКТУРА СТАНУ КЛІМАТ-СИСТЕМИ ===
struct ClimateState {
    // Налаштування (з Preferences)
    float set_temp_day = 25.0;
    float set_hum_limit = 50.0;
    float tempOffset = 0.0;
    float hysteresis = 0.1;
    
    // Поточний стан сенсорів
    float lastValidT = NAN;
    float lastValidH = NAN;
    bool isDay = true;
    
    // Стан виконавчих механізмів
    int currentActiveChannel = 0;
    bool currentHeatState = false;
    bool tooColdLock = false;
    
    // Автоматичні цикли
    AutoCycle activeCycle = outNormal;
    float autoOffset = 0.0;
    unsigned long lastCycleChangeTime = 0;
    
    // Kickstart
    bool kickstartActive = false;
    unsigned long kickstartTime = 0;
    int targetChannelAfterKick = 0;
    
    // Реле черга
    int pendingChannel = -1;
    unsigned long lastFanSwitchTime = 0;
    
    // Режими
    bool systemOn = true;
    bool manualBoost = false;
    
    // Діагностика
    int dhtRetryCount = 0;
};

// === ІНІЦІАЛІЗАЦІЯ ===
void climateInit(ClimateState* state);

// === КЕРУВАННЯ РЕЛЕ ===
void handleRelayQueue(ClimateState* state);
void setFanChannel(ClimateState* state, int channel);
void startFanWithKick(ClimateState* state, int targetChannel);
void heatControl(ClimateState* state, bool state_heat);

// === АВТОМАТИЧНІ ЦИКЛИ ===
void selectCycleOnBoot(ClimateState* state, float t);
void checkCycleTransition(ClimateState* state, int newChannel);

// === ГОЛОВНА ЛОГІКА ===
void runClimateControl(ClimateState* state);

#endif // CLIMATE_H