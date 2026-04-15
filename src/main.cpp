// main.cpp
#define BLYNK_TEMPLATE_ID "TMPL47MHSCoFY"
#define BLYNK_TEMPLATE_NAME "Fazenda"

#include "secrets.h"
#include "config.h"
#include "display.h"
#include "climate.h"
#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <SPI.h>
#include "DHT.h"
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPIFFS.h>
#include <Update.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "webui.h"
#include <time.h>
#include "logger.h"
#include "network.h"

// === FORWARD DECLARATIONS ===
void checkPreferences();
void checkBlynkStatus();
void sendPeriodicData();
void pushChangesToBlynk();

// === TIMING КОНСТАНТИ ===
const long CLIMATE_CHECK_INTERVAL = 10000;
const unsigned long PREF_WRITE_DELAY = 5000;

// --- ОБ'ЄКТИ ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);
Preferences pref;
BlynkTimer timer;

// === КЛІМАТ-СТАН ===
ClimateState climate;

// === ІНШІ ГЛОБАЛЬНІ ЗМІННІ ===
unsigned long lastReadTime = 0;
bool prefChanged = false;
unsigned long lastPrefChangeTime = 0;
bool blynkSyncPending = false;
unsigned long blynkSyncAt = 0;

// === BLYNK МЕРЕЖЕВІ ФУНКЦІЇ ===
void checkBlynkStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!Blynk.connected()) {
            esp_task_wdt_reset();
            Blynk.connect(3000);
            esp_task_wdt_reset();
        }
    } else {
        static unsigned long lastWiFiRetry = 0;
        if (millis() - lastWiFiRetry > 30000) {
            esp_task_wdt_reset();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            lastWiFiRetry = millis();
            Serial.println("WiFi lost. Retrying...");
        }
    }
}

void sendPeriodicData() {
    if (Blynk.connected() && !isnan(climate.lastValidT)) {
        Blynk.virtualWrite(V1, climate.lastValidT);
        Blynk.virtualWrite(V2, climate.lastValidH);
    }
}

void pushChangesToBlynk() {
    static int lastSentFan = -1;
    static int lastSentHeat = -1;
    static bool lastSentTooCold = false;
    static bool lastSentDHTErr = false;
    static bool lastSentDayStatus = true;

    if (!Blynk.connected()) return;

    if (climate.currentActiveChannel != lastSentFan) {
        Blynk.virtualWrite(V3, climate.currentActiveChannel);
        lastSentFan = climate.currentActiveChannel;
    }

    if (climate.currentHeatState != (lastSentHeat == 1)) {
        Blynk.virtualWrite(V4, climate.currentHeatState ? 1 : 0);
        lastSentHeat = climate.currentHeatState ? 1 : 0;
    }

    if (climate.isDay != lastSentDayStatus) {
        Blynk.virtualWrite(V7, climate.isDay ? 1 : 0);
        Blynk.virtualWrite(V8, climate.isDay ? 0 : 1);
        lastSentDayStatus = climate.isDay;
    }

    if (climate.tooColdLock != lastSentTooCold) {
        Blynk.virtualWrite(V9, climate.tooColdLock ? 1 : 0);
        lastSentTooCold = climate.tooColdLock;
    }

    bool dhtErr = (climate.dhtRetryCount >= 3);
    if (dhtErr != lastSentDHTErr) {
        Blynk.virtualWrite(V11, dhtErr ? 1 : 0);
        lastSentDHTErr = dhtErr;
    }
}

// --- СИСТЕМНІ ФУНКЦІЇ ---
void resetBootDiagnostics() {
    pref.putUInt("bootCnt", 0);
    pref.putFloat("lastT", NAN);
    pref.putInt("lastCh", 0);
    pref.putFloat("lastOff", 0.0);
    pref.putUInt("lastErr", 0);
    pref.putULong("lastUp", 0);

    Serial.println("\n=== BOOT DIAGNOSTICS RESET ===");
    Serial.println("All counters cleared!");
    Serial.println("==============================\n");
}

void checkPreferences() {
    if (prefChanged && (millis() - lastPrefChangeTime >= PREF_WRITE_DELAY)) {
        pref.putFloat("temp", climate.set_temp_day);
        pref.putFloat("hum", climate.set_hum_limit);
        pref.putFloat("offset", climate.tempOffset);
        pref.putFloat("hyst", climate.hysteresis);
        pref.putBool("sysOn", climate.systemOn);
        prefChanged = false;
        Serial.println("Settings saved to Flash!");
    }
}

// --- BLYNK ---
BLYNK_CONNECTED() {
    Blynk.syncVirtual(V5);
    Blynk.syncVirtual(V6);
    // Відкладаємо push на 500мс — щоб syncVirtual встиг отримати відповідь від сервера
    blynkSyncPending = true;
    blynkSyncAt = millis() + 500;
}

BLYNK_WRITE(V5) {
    float val = param.asFloat();
    Serial.printf(">>> V5 RAW: %.2f\n", val);
    climate.set_temp_day = constrain(val, 10.0, 35.0);
    Serial.printf(">>> set_temp_day: %.2f\n", climate.set_temp_day);
    prefChanged = true;
    lastPrefChangeTime = millis();
}

BLYNK_WRITE(V6) {
    float val = param.asFloat();
    climate.set_hum_limit = constrain(val, 30.0, 90.0);
    prefChanged = true;
    lastPrefChangeTime = millis();
    Serial.printf("New Hum: %.1f\n", climate.set_hum_limit);
}

BLYNK_WRITE(V0) {
    bool newBoost = (param.asInt() == 1);

    if (newBoost != climate.manualBoost) {
        Serial.printf("[V0] Boost: %s \xe2\x86\x92 %s\n",
            climate.manualBoost ? "ON" : "OFF", newBoost ? "ON" : "OFF");
    }

    if (newBoost && !climate.systemOn) {
        Serial.println("[V0] Boost ignored \xe2\x80\x94 system OFF");
        Blynk.virtualWrite(V0, 0);
        return;
    }

    climate.manualBoost = newBoost;

    if (climate.manualBoost) {
        Serial.println("[V0] Forcing CH4");
        startFanWithKick(&climate, 4);
    } else {
        Serial.println("[V0] Boost OFF");
    }
}

BLYNK_WRITE(V10) {
    climate.systemOn = (param.asInt() == 1);
    prefChanged = true;
    lastPrefChangeTime = millis();

    Serial.printf("[V10] System: %s\n", climate.systemOn ? "ON" : "OFF");

    if (!climate.systemOn) {
        Serial.println("[V10] Stopping fan and heater");
        setFanChannel(&climate, 0);
        heatControl(&climate, false);

        if (climate.isDay) {
            climate.activeCycle = outNormal;
            climate.autoOffset = 0.0;
            climate.bootCycleSelected = false;
            Serial.println("[V10] Cycle reset to outNormal");
        }
    }
}

BLYNK_WRITE(V13) {
    int val = param.asInt();
    climate.tempOffset = constrain(val, -20, 20) / 10.0;

    prefChanged = true;
    lastPrefChangeTime = millis();

    Serial.printf("V13: %d \xe2\x86\x92 Offset: %.1f\n", val, climate.tempOffset);

    if (Blynk.connected()) {
        Blynk.virtualWrite(V14, climate.tempOffset);
    }
}

BLYNK_WRITE(V15) {
    int val = param.asInt();
    float oldHyst = climate.hysteresis;
    climate.hysteresis = constrain(val, 1, 50) / 10.0;
    prefChanged = true;
    lastPrefChangeTime = millis();

    Serial.printf("[V15] Hyst: %.2f \xe2\x86\x92 %.2f\n", oldHyst, climate.hysteresis);
}

void setup() {
    Serial.begin(115200);

    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    drawStaticUI();

    pinMode(RELAY_CH1_PIN, OUTPUT);  digitalWrite(RELAY_CH1_PIN, HIGH);
    pinMode(RELAY_CH2_PIN, OUTPUT);  digitalWrite(RELAY_CH2_PIN, HIGH);
    pinMode(RELAY_CH3_PIN, OUTPUT);  digitalWrite(RELAY_CH3_PIN, HIGH);
    pinMode(RELAY_CH4_PIN, OUTPUT);  digitalWrite(RELAY_CH4_PIN, HIGH);
    pinMode(RELAY_HEAT_PIN, OUTPUT); digitalWrite(RELAY_HEAT_PIN, HIGH);

    dht.begin();
    delay(2000);

    float initialT = dht.readTemperature();
    float initialH = dht.readHumidity();

    if (!isnan(initialT) && !isnan(initialH)) {
        Serial.printf("[BOOT] Initial: T=%.1f\xc2\xb0\x43 H=%.1f%%\n", initialT, initialH);
        updateDisplayNew(initialT, initialH, 0, true, false, false, outNormal, true, false);
    } else {
        Serial.println("[BOOT] DHT not ready - showing ERR");
    }

    pref.begin("fazenda", false);

    // === ІНІЦІАЛІЗАЦІЯ CLIMATE МОДУЛЯ ===
    climateInit(&climate);

    // === ІНІЦІАЛІЗАЦІЯ SPIFFS ===
    Serial.println("\n========== SPIFFS INIT ==========");
    logger.storageAvailable = initSPIFFS();
    if (logger.storageAvailable) {
        Serial.println("[SPIFFS] OK - Logging enabled");
        logEvent("BOOT", "System started v2.5.0");
    } else {
        Serial.println("[SPIFFS] WARNING - Logging disabled");
    }
    Serial.println("==================================\n");

    // Завантаження налаштувань
    climate.set_temp_day = constrain(pref.getFloat("temp", 25.0), 10.0, 35.0);
    climate.set_hum_limit = constrain(pref.getFloat("hum", 50.0), 30.0, 90.0);
    climate.tempOffset = constrain(pref.getFloat("offset", 0.0), -2.0, 2.0);
    climate.hysteresis = constrain(pref.getFloat("hyst", 0.1), 0.1, 4.0);
    climate.systemOn = pref.getBool("sysOn", true);

    climate.lastValidT = initialT;
    climate.lastValidH = initialH;

    // === ДІАГНОСТИКА ПЕРЕЗАВАНТАЖЕНЬ ===
    uint32_t bootCount = pref.getUInt("bootCnt", 0) + 1;
    pref.putUInt("bootCnt", bootCount);

    float lastTemp = pref.getFloat("lastT", NAN);
    int lastChannel = pref.getInt("lastCh", 0);
    float lastOffset = pref.getFloat("lastOff", 0.0);
    uint32_t lastErrors = pref.getUInt("lastErr", 0);
    unsigned long lastUptime = pref.getULong("lastUp", 0);

    Serial.println("\n========== BOOT DIAGNOSTICS ==========");
    Serial.printf("Boot count: %u\n", bootCount);
    Serial.printf("Previous uptime: %lu seconds\n", lastUptime / 1000);
    Serial.printf("Last temperature: %.1f\xc2\xb0\x43\n", lastTemp);
    Serial.printf("Last fan channel: %d\n", lastChannel);
    Serial.printf("Last offset: %.1f\xc2\xb0\x43\n", lastOffset);
    Serial.printf("Last DHT errors: %u\n", lastErrors);
    Serial.println("======================================\n");

    if (bootCount > 10) {
        Serial.println("WARNING: More than 10 reboots detected!");
    }

    if (bootCount > 1 && lastUptime < 60000) {
        Serial.println("WARNING: Last session was very short (<60 sec)!");
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
        delay(200);
        esp_task_wdt_reset();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi OK: " + WiFi.localIP().toString());

        initTime();

        if (!timeInitialized) {
            Serial.println("[NTP] First attempt failed, retrying...");
            delay(3000);
            initTime();
        }
    } else {
        Serial.println("WiFi failed, will retry in loop");
    }

    esp_task_wdt_init(15, true);
    esp_task_wdt_add(NULL);

    Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 80);
    Blynk.connect(5000);
    esp_task_wdt_reset();
    Serial.println(Blynk.connected() ? "Blynk OK" : "Blynk FAILED");

    timer.setInterval(60000L, sendPeriodicData);
    timer.setInterval(30000L, checkBlynkStatus);

    lastReadTime = millis() - CLIMATE_CHECK_INTERVAL;

    Serial.println("System initialized. Logic will start in loop().");

    Serial.println("\n========== AUTO CYCLE INFO ==========");
    Serial.printf("Initial cycle: %s (offset: %.1f)\n",
        climate.activeCycle == outCold ? "outCold" :
        climate.activeCycle == outNormal ? "outNormal" : "outHot",
        climate.autoOffset);
    Serial.println("Auto cycles work ONLY during DAY mode");
    Serial.println("====================================\n");

    setupWebServer();


    // === ВИВЕСТИ ЛОГ У SERIAL ===
    if (logger.storageAvailable) {
        delay(1000);
        File file = SPIFFS.open("/climate.log", FILE_READ);
        if (file) {
            Serial.println("\n========== CLIMATE LOG CONTENT ==========");
            while (file.available()) {
                Serial.write(file.read());
            }
            file.close();
            Serial.println("==========================================\n");
        } else {
            Serial.println("[ERROR] Could not open /climate.log");
        }
    }
}

void loop() {
    esp_task_wdt_reset();

    // === ДІАГНОСТИКА (кожні 30 сек) ===
    static unsigned long lastBootDiagUpdate = 0;
    if (millis() - lastBootDiagUpdate > 30000) {
        pref.putULong("lastUp", millis());
        pref.putFloat("lastT", climate.lastValidT);
        pref.putInt("lastCh", climate.currentActiveChannel);
        pref.putUInt("lastErr", climate.dhtRetryCount);
        pref.putFloat("lastOff", climate.tempOffset);
        lastBootDiagUpdate = millis();
    }

    // 1. ПРІОРИТЕТ: Реле + Анімація
    handleRelayQueue(&climate);
    processChannelAnimation();

    // 2. Оновлення дисплея
    static int lastShownFan = -1;
    static bool lastShownHeat = false;
    static float lastShownT = -999.0;
    static float lastShownH = -999.0;
    static bool lastShownDay = !climate.isDay;
    static AutoCycle lastShownCycle = outNormal;
    static bool lastShownSystemOn = true;

    bool tChanged = (isnan(climate.lastValidT) != isnan(lastShownT)) ||
                    (!isnan(climate.lastValidT) && fabsf(climate.lastValidT - lastShownT) > 0.05);
    bool hChanged = (isnan(climate.lastValidH) != isnan(lastShownH)) ||
                    (!isnan(climate.lastValidH) && fabsf(climate.lastValidH - lastShownH) > 0.5);

    if (climate.currentActiveChannel != lastShownFan ||
        climate.currentHeatState != lastShownHeat ||
        tChanged || hChanged ||
        climate.isDay != lastShownDay ||
        climate.activeCycle != lastShownCycle ||
        climate.systemOn != lastShownSystemOn) {

        updateDisplayNew(climate.lastValidT, climate.lastValidH,
                        climate.currentActiveChannel, climate.isDay,
                        climate.currentHeatState, climate.tooColdLock,
                        climate.activeCycle, climate.systemOn,
                        Blynk.connected());

        lastShownFan = climate.currentActiveChannel;
        lastShownHeat = climate.currentHeatState;
        lastShownT = climate.lastValidT;
        lastShownH = climate.lastValidH;
        lastShownDay = climate.isDay;
        lastShownCycle = climate.activeCycle;
        lastShownSystemOn = climate.systemOn;
    }

    // 3. Мережа
    if (Blynk.connected()) Blynk.run();
    timer.run();
    checkPreferences();

    // === BLYNK SYNC (відкладений після реконнекту) ===
    if (blynkSyncPending && millis() >= blynkSyncAt && Blynk.connected()) {
        blynkSyncPending = false;
        Blynk.syncVirtual(V5);
        Blynk.virtualWrite(V6, climate.set_hum_limit);
        Blynk.virtualWrite(V13, (int)(climate.tempOffset * 10));
        Blynk.virtualWrite(V0, climate.manualBoost ? 1 : 0);
        Blynk.virtualWrite(V10, climate.systemOn ? 1 : 0);
        Blynk.virtualWrite(V4, climate.currentHeatState ? 1 : 0);
        Blynk.virtualWrite(V11, (climate.dhtRetryCount >= 3) ? 1 : 0);
        Blynk.virtualWrite(V15, (int)(climate.hysteresis * 10));
        Serial.println("All Blynk values synced!");
    }

    // === NTP RETRY ===
    static unsigned long lastNtpRetry = 0;

    if (!timeInitialized && WiFi.status() == WL_CONNECTED &&
        millis() - lastNtpRetry > 60000) {
        Serial.println("[NTP] Retrying time sync...");
        esp_task_wdt_reset();
        initTime();
        esp_task_wdt_reset();
        lastNtpRetry = millis();
    }

    // Періодичне логування
    logPeriodicData();

    // Очищення логів
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 3600000) {
        cleanOldLogs();
        lastCleanup = millis();
    }

    // 4. Kickstart
    if (climate.kickstartActive &&
        (millis() - climate.kickstartTime >= 5000)) {
        climate.kickstartActive = false;

        if (climate.targetChannelAfterKick != climate.currentActiveChannel) {
            setFanChannel(&climate, climate.targetChannelAfterKick);
            Serial.printf("Kickstart finished. Target: %d\n",
                climate.targetChannelAfterKick);
        }
    }

    // 5. Клімат-контроль
    if (millis() - lastReadTime >= CLIMATE_CHECK_INTERVAL) {
        lastReadTime = millis();
        runClimateControl(&climate);
        pushChangesToBlynk();
    }

    // === ОЧИСТИТИ ЛОГИ (Serial 'C') ===
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'C' || cmd == 'c') {
            Serial.println("\n[MANUAL] Clearing logs...");

            SPIFFS.remove("/climate.log");

            File file = SPIFFS.open("/climate.log", FILE_WRITE);
            if (file) {
                file.println("=== FAZENDA CLIMATE LOG ===");
                file.println("Timestamp,Temp,Hum,Fan,Heat,Mode,Event");
                file.close();
            }

            logger.ch1_activations = 0;
            logger.ch2_activations = 0;
            logger.ch3_activations = 0;
            logger.ch4_activations = 0;
            logger.overheat_events = 0;
            logger.dht_errors = 0;
            logger.coldlock_events = 0;

            logEvent("SYSTEM", "Logs cleared manually");
            Serial.println("[MANUAL] Done!\n");
        }
    }
}
