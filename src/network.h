#ifndef NETWORK_H
#define NETWORK_H

#include <ESPAsyncWebServer.h>

// === EXTERN ЗМІННІ ===
extern AsyncWebServer server;
extern bool isAuthenticated;

// === ФУНКЦІЇ ===
void initTime();
void setupWebServer();

#endif // NETWORK_H
