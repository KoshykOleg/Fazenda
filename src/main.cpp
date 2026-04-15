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

// === FORWARD DECLARATIONS ===
void initTime();
String getTimestamp();
bool initSPIFFS();
void logToFile(const char* message);
void logPeriodicData();
void logFanChangeEvent(int oldCh, int newCh, float temp);
void logEvent(const char* eventType, const char* message);
void cleanOldLogs();
void checkPreferences();
void setupWebServer();

// === TIMING КОНСТАНТИ ===
const long CLIMATE_CHECK_INTERVAL = 10000;
const unsigned long PREF_WRITE_DELAY = 5000;

DataLogger logger;

// --- ОБ'ЄКТИ ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);
Preferences pref;
BlynkTimer timer;

// === ВЕБ-СЕРВЕР ===
AsyncWebServer server(80);
bool isAuthenticated = false;

// === ЧАС ===
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 2 * 3600;      
const int daylightOffset_sec = 0;
bool timeInitialized = false;

// === КЛІМАТ-СТАН ===
ClimateState climate;

// === ІНШІ ГЛОБАЛЬНІ ЗМІННІ ===
unsigned long lastReadTime = 0;
bool prefChanged = false;
unsigned long lastPrefChangeTime = 0;

// === SPIFFS ЛОГУВАННЯ ===
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

void logToFile(const char* message) {
    if (!logger.storageAvailable) return;
    
    File file = SPIFFS.open("/climate.log", FILE_APPEND);
    if (!file) {
        Serial.println("[SPIFFS] Failed to open log file");
        return;
    }

    String timestamp = getTimestamp();
    file.printf("%s %s\n", timestamp.c_str(), message);
    file.close();
}

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
    
    Serial.printf("[LOGGER] Fan: CH%d → CH%d (Stats: CH1=%d CH2=%d CH3=%d CH4=%d)\n",
        oldChannel, newChannel, 
        logger.ch1_activations, logger.ch2_activations, 
        logger.ch3_activations, logger.ch4_activations);
}

void logEvent(const char* eventType, const char* details = "") {
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

// === ІНІЦІАЛІЗАЦІЯ ЧАСУ ===
void initTime() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    Serial.print("[NTP] Waiting for time sync");
    struct tm timeinfo;
    int attempts = 0;
    
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        esp_task_wdt_reset();
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (attempts < 10) {
        Serial.println(" OK");
        Serial.printf("[NTP] Current time: %02d:%02d:%02d %02d.%02d.%04d\n",
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
            timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        timeInitialized = true;
    } else {
        Serial.println(" FAILED");
        Serial.println("[NTP] Will continue without real time");
        timeInitialized = false;
    }
}

String getTimestamp() {
    struct tm timeinfo;
    if (timeInitialized && getLocalTime(&timeinfo)) {
        char buffer[64];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return String(buffer);
    }
    
    unsigned long uptime = millis() / 1000;
    return "[" + String(uptime) + "s]";
}

// === ВЕБ-СЕРВЕР ===
void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", HTML_LOGIN);
    });
    
    server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("password", true)) {
            String password = request->getParam("password", true)->value();
            if (password == String(OTA_PASSWORD)) {
                isAuthenticated = true;
                request->send(200, "text/plain", "OK");
            } else {
                request->send(401, "text/plain", "Wrong password");
            }
        } else {
            request->send(400, "text/plain", "Missing password");
        }
    });
    
    server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request){
        isAuthenticated = false;
        request->redirect("/");
    });
    
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthenticated) {
            request->redirect("/");
            return;
        }
        request->send(200, "text/html", HTML_UPDATE);
    });
    
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthenticated) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        
        String json = "{";
        json += "\"temp\":" + String(climate.lastValidT, 1) + ",";
        json += "\"hum\":" + String(climate.lastValidH, 1) + ",";
        json += "\"fan\":" + String(climate.currentActiveChannel) + ",";
        json += "\"mode\":\"" + String(climate.isDay ? "DAY" : "NIGHT") + "\"";
        json += "}";
        
        request->send(200, "application/json", json);
    });
    
    server.on("/update", HTTP_POST, 
        [](AsyncWebServerRequest *request){
            if (!isAuthenticated) {
                request->send(401, "text/plain", "Unauthorized");
                return;
            }
            
            bool shouldReboot = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", 
                shouldReboot ? "OK" : "FAIL");
            response->addHeader("Connection", "close");
            request->send(response);
            
            if (shouldReboot) {
                delay(1000);
                ESP.restart();
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
            if (!isAuthenticated) return;
            
            if (!index) {
                Serial.printf("OTA Update Start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("OTA Update Success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );
    
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthenticated) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        
        if (!logger.storageAvailable) {
            request->send(500, "text/plain", "SPIFFS not available");
            return;
        }

        String stats = "=== FAZENDA CLIMATE LOGS ===\n\n";
        stats += "STATISTICS:\n";
        stats += "CH1 activations: " + String(logger.ch1_activations) + "\n";
        stats += "CH2 activations: " + String(logger.ch2_activations) + "\n";
        stats += "CH3 activations: " + String(logger.ch3_activations) + "\n";
        stats += "CH4 activations: " + String(logger.ch4_activations) + "\n";
        stats += "Overheat events: " + String(logger.overheat_events) + "\n";
        stats += "DHT errors: " + String(logger.dht_errors) + "\n";
        stats += "Coldlock events: " + String(logger.coldlock_events) + "\n\n";
        stats += "=== LOG ENTRIES ===\n";

        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/plain",
            [stats](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                static File logFile;
                
                if (index < stats.length()) {
                    size_t toCopy = min(maxLen, stats.length() - index);
                    memcpy(buffer, stats.c_str() + index, toCopy);
                    return toCopy;
                }
                
                size_t fileIndex = index - stats.length();
                
                if (fileIndex == 0) {
                    logFile = SPIFFS.open("/climate.log", FILE_READ);
                    if (!logFile) return 0;
                }
                
                if (!logFile || !logFile.available()) {
                    if (logFile) logFile.close();
                    return 0;
                }
                
                return logFile.read(buffer, maxLen);
            }
        );
        
        request->send(response);
    });
    
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not Found");
    });
    
    server.begin();
    Serial.println("[WEB] Server started on port 80");
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

    timer.setTimeout(500, []() {
        Blynk.syncVirtual(V5);
        Blynk.virtualWrite(V6, climate.set_hum_limit);
        Blynk.virtualWrite(V13, (int)(climate.tempOffset * 10)); 
        Blynk.virtualWrite(V0, climate.manualBoost ? 1 : 0);
        Blynk.virtualWrite(V10, climate.systemOn ? 1 : 0);
        Blynk.virtualWrite(V4, climate.currentHeatState ? 1 : 0);
        Blynk.virtualWrite(V11, (climate.dhtRetryCount >= 3) ? 1 : 0);
        Blynk.virtualWrite(V15, (int)(climate.hysteresis * 10));
        
        Serial.println("All Blynk values synced!");
    });
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
        Serial.printf("[V0] Boost: %s → %s\n", 
            climate.manualBoost ? "ON" : "OFF", newBoost ? "ON" : "OFF");
    }
    
    if (newBoost && !climate.systemOn) {
        Serial.println("[V0] Boost ignored — system OFF");
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
    
    Serial.printf("V13: %d → Offset: %.1f\n", val, climate.tempOffset);
    
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
    
    Serial.printf("[V15] Hyst: %.2f → %.2f\n", oldHyst, climate.hysteresis);
}

void sendPeriodicData() {
    if (Blynk.connected() && !isnan(climate.lastValidT)) {
        Blynk.virtualWrite(V1, climate.lastValidT); 
        Blynk.virtualWrite(V2, climate.lastValidH);
    }
}

// === СИНХРОНІЗАЦІЯ З BLYNK ===
void pushChangesToBlynk(ClimateState* state) {
    static int lastSentFan = -1;
    static int lastSentHeat = -1;
    static bool lastSentTooCold = false;
    static bool lastSentDHTErr = false;
    static bool lastSentDayStatus = true;
    
    if (!Blynk.connected()) return;

    if (state->currentActiveChannel != lastSentFan) {
        Blynk.virtualWrite(V3, state->currentActiveChannel);
        lastSentFan = state->currentActiveChannel;
    }
    
    if (state->currentHeatState != (lastSentHeat == 1)) {
        Blynk.virtualWrite(V4, state->currentHeatState ? 1 : 0);
        lastSentHeat = state->currentHeatState ? 1 : 0;
    }

    if (state->isDay != lastSentDayStatus) {
        Blynk.virtualWrite(V7, state->isDay ? 1 : 0);
        Blynk.virtualWrite(V8, state->isDay ? 0 : 1);
        lastSentDayStatus = state->isDay;
    }

    if (state->tooColdLock != lastSentTooCold) {
        Blynk.virtualWrite(V9, state->tooColdLock ? 1 : 0);
        lastSentTooCold = state->tooColdLock;
    }
    
    bool dhtErr = (state->dhtRetryCount >= 1);
    if (dhtErr != lastSentDHTErr) {
        Blynk.virtualWrite(V11, dhtErr ? 1 : 0);
        lastSentDHTErr = dhtErr;
    }
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
        Serial.printf("[BOOT] Initial: T=%.1f°C H=%.1f%%\n", initialT, initialH);
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
    Serial.printf("Last temperature: %.1f°C\n", lastTemp);
    Serial.printf("Last fan channel: %d\n", lastChannel);
    Serial.printf("Last offset: %.1f°C\n", lastOffset);
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
        pushChangesToBlynk(&climate);
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