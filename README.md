# 🌱 Fazenda Climate Control

Система автоматичного кліматконтролю на базі ESP32 з веб-інтерфейсом та OTA оновленнями.

## ⚡ Можливості

- 🌡️ Моніторинг температури та вологості (DHT22)
- 💨 4-канальне управління вентиляцією
- 🔥 Автоматичний обігрів
- 📊 TFT дисплей ST7735 (1.8")
- 🌐 Веб-інтерфейс для OTA прошивки
- 📝 SPIFFS логування з NTP часом
- 📱 Інтеграція з Blynk
- 🔍 Діагностика перезавантажень

## 🛠️ Обладнання

- ESP32 DevKit
- DHT22 (температура/вологість)
- ST7735 TFT дисплей 1.8"
- 4-канальний модуль реле
- Фоторезистор (датчик світла)
- Силові реле для обігріву

## 📦 Встановлення

1. Клонуй репозиторій:
```bash
git clone https://github.com/KoshykOleg/fazenda-climate.git
cd fazenda-climate
```

2. Створи `src/secrets.h` з `secrets.example.h`:
```bash
cp src/secrets.example.h src/secrets.h
```

3. Відредагуй `src/secrets.h`:
```cpp
#define WIFI_SSID "твій_wifi"
#define WIFI_PASS "твій_пароль"
#define BLYNK_AUTH_TOKEN "твій_токен_blynk"
#define OTA_PASSWORD "пароль_для_ота"
```

4. Завантаж прошивку:
```bash
pio run -t upload
```

## 🌐 Веб-інтерфейс

### OTA оновлення
1. Відкрий `http://192.168.0.100/`
2. Введи OTA пароль
3. Завантаж `.bin` файл з `.pio/build/esp32dev/`

### Перегляд логів
- `http://192.168.0.100/logs`

### Очищення логів
- Serial Monitor → `C` + Enter

## 📊 API Endpoints
```
GET  /              - Сторінка входу
POST /login         - Авторізація
GET  /logout        - Вихід
GET  /update        - Сторінка OTA
POST /update        - Завантаження firmware
GET  /status        - JSON статус системи
GET  /logs          - Перегляд логів
```

## 📝 Формат логів
```
2026-04-01 21:33:23 50,23.0,62.1,3,0,DAY,FAN_CHANGE:CH0->CH3
```

**Розшифровка:**
- `2026-04-01 21:33:23` — дата/час (NTP)
- `50` — uptime (секунди)
- `23.0` — температура (°C)
- `62.1` — вологість (%)
- `3` — активний канал вентилятора
- `0` — обігрів (0/1)
- `DAY` — режим (DAY/NIGHT)
- `FAN_CHANGE:CH0->CH3` — подія

## 🔧 Налаштування

### Константи логіки
```cpp
COLDLOCK_TEMP_LOW     18.0°C  // Холодне блокування
NIGHT_TEMP_OFFSET     4.0°C   // Зсув температури вночі
CLIMATE_CHECK_INTERVAL 10 сек // Інтервал читання DHT
```

### Управління через Blynk
- **V5** - Цільова температура
- **V6** - Цільова вологість
- **V0** - Manual Boost (CH4)
- **V10** - Система ON/OFF
- **V13** - Offset температури

## 📈 Статистика

Логи зберігають:
- Активації каналів (CH1-4)
- Події перегріву
- Помилки DHT22
- Активації coldlock

## 🐛 Діагностика

### Boot Diagnostics
- Лічильник перезавантажень
- Останні значення температури/вологості
- Попередній uptime
- Помилки датчика

### Скинути лічильник:
Розкоментуй у `setup()`:
```cpp
resetBootDiagnostics();
```

## 🔐 Безпека

- Авторізація для OTA
- `.gitignore` для `secrets.h`
- Паролі не зберігаються в репозиторії

## 📡 Мережеві налаштування

- **IP:** 192.168.0.100 (статичний)
- **Порт:** 80 (веб-сервер)
- **Blynk:** blynk.cloud:80

## 📚 Залежності
```ini
lib_deps = 
    https://github.com/mathieucarbou/ESPAsyncWebServer#v3.3.15
    mathieucarbou/AsyncTCP@^3.2.14
    blynkkk/Blynk@^1.3.2
    adafruit/DHT sensor library@^1.4.6
    adafruit/Adafruit ST7735 and ST7789 Library@^1.10.4
```

## 📄 Ліцензія

MIT License - вільне використання та модифікація

## 👨‍💻 Автор

KoshykOleg - [GitHub](https://github.com/KoshykOleg/fazenda-climate)

## 🙏 Подяки

- Anthropic Claude за допомогу в розробці
- ESP32 community
- Blynk team