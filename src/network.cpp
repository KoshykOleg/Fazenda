#include "network.h"
#include "climate.h"
#include "logger.h"
#include "webui.h"
#include "secrets.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <time.h>
#include <esp_task_wdt.h>

// === EXTERN ЗАЛЕЖНОСТІ ===
extern ClimateState climate;
extern Preferences pref;
extern bool timeInitialized;
extern const char* ntpServer;
extern const long gmtOffset_sec;
extern const int daylightOffset_sec;

// === ГЛОБАЛЬНІ ЗМІННІ ===
AsyncWebServer server(80);
bool isAuthenticated = false;

// === ІНІЦІАЛІЗАЦІЯ NTP ЧАСУ ===
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

// === ВЕБ-СЕРВЕР SETUP ===
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

        struct LogStreamCtx {
            String stats;
            File file;
        };

        LogStreamCtx* ctx = new LogStreamCtx();
        ctx->stats  = "=== FAZENDA CLIMATE LOGS ===\n\n";
        ctx->stats += "STATISTICS:\n";
        ctx->stats += "CH1 activations: " + String(logger.ch1_activations) + "\n";
        ctx->stats += "CH2 activations: " + String(logger.ch2_activations) + "\n";
        ctx->stats += "CH3 activations: " + String(logger.ch3_activations) + "\n";
        ctx->stats += "CH4 activations: " + String(logger.ch4_activations) + "\n";
        ctx->stats += "Overheat events: " + String(logger.overheat_events) + "\n";
        ctx->stats += "DHT errors: "      + String(logger.dht_errors)      + "\n";
        ctx->stats += "Coldlock events: " + String(logger.coldlock_events)  + "\n\n";
        ctx->stats += "=== LOG ENTRIES ===\n";

        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/plain",
            [ctx](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                if (index < ctx->stats.length()) {
                    size_t toCopy = min(maxLen, ctx->stats.length() - index);
                    memcpy(buffer, ctx->stats.c_str() + index, toCopy);
                    return toCopy;
                }

                size_t fileIndex = index - ctx->stats.length();

                if (fileIndex == 0) {
                    ctx->file = SPIFFS.open("/climate.log", FILE_READ);
                    if (!ctx->file) {
                        delete ctx;
                        return 0;
                    }
                }

                if (!ctx->file.available()) {
                    ctx->file.close();
                    delete ctx;
                    return 0;
                }

                return ctx->file.read(buffer, maxLen);
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
