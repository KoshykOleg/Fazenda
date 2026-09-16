#pragma once
#include <cmath>

#ifndef DHT22
#define DHT22 22
#endif

class DHT {
public:
    float _fakeTemp = 25.0f;
    float _fakeHum = 50.0f;
    bool _fakeError = false;
    long reads = 0;

    DHT() {}
    DHT(int, int) {}

    void begin() {}
    float readTemperature() { reads++; return _fakeError ? NAN : _fakeTemp; }
    float readHumidity() { return _fakeError ? NAN : _fakeHum; }
};
