# Fazenda Climate Control

Система автоматичного клімат-контролю для теплиці на базі ESP32 з веб-інтерфейсом та OTA оновленнями.

## Можливості

- Моніторинг температури та вологості (DHT22)
- 4-канальне управління вентиляцією з кікстартом
- Автоматичні цикли (outCold / outNormal / outHot)
- Захист від переохолодження (ColdLock)
- Автоматичне перемикання День/Ніч (фоторезистор)
- Нічна логіка з температурною адаптацією та вологісним контролем
- TFT дисплей ST7735 1.8" з анімацією каналів
- Веб-інтерфейс: OTA прошивка, перегляд логів, статус JSON
- SPIFFS логування з NTP часом (ротація файлів)
- Інтеграція з Blynk IoT
- Діагностика перезавантажень у Flash

## Обладнання

| Компонент | Підключення |
|-----------|-------------|
| ESP32 DevKit | — |
| DHT22 | GPIO 4 |
| ST7735 TFT 1.8" | CS=5, DC=2, RST=15, MOSI=23, SCLK=22 |
| Реле CH1–CH4 | GPIO 14, 27, 26, 32 |
| Реле обігрів | GPIO 19 |
| Фоторезистор | GPIO 34 |

## Структура проекту

```
src/
├── main.cpp        — setup(), loop(), BLYNK_WRITE handlers
├── climate.cpp/h   — логіка клімат-контролю, state machine
├── display.cpp/h   — TFT дисплей, анімації
├── logger.cpp/h    — SPIFFS логування, NTP змінні, getTimestamp
├── network.cpp/h   — initTime (NTP), setupWebServer (OTA + /logs)
├── config.h        — піни, константи, DBG макрос
├── webui.h         — HTML сторінки
└── secrets.h       — credentials (не в репо)
```

## Встановлення

1. Клонуй репозиторій:
```bash
git clone https://github.com/KoshykOleg/Fazenda.git
cd Fazenda
```

2. Створи `src/secrets.h` з шаблону:
```bash
cp src/secrets.example.h src/secrets.h
```

3. Заповни `src/secrets.h`:
```cpp
#define WIFI_SSID       "твій_wifi"
#define WIFI_PASS       "твій_пароль"
#define BLYNK_AUTH_TOKEN "твій_токен_blynk"
#define OTA_PASSWORD    "пароль_для_ота"
```

4. Завантаж прошивку:
```bash
pio run -t upload
```

## Веб-інтерфейс

### API Endpoints
```
GET  /        — Сторінка входу
POST /login   — Авторизація (password)
GET  /logout  — Вихід
GET  /update  — Сторінка OTA
POST /update  — Завантаження firmware (.bin)
GET  /status  — JSON статус системи
GET  /logs    — Перегляд логів + статистика
```

### OTA оновлення
1. Відкрий `http://<IP пристрою>/`
2. Введи OTA пароль
3. Завантаж `.bin` з `.pio/build/esp32dev/firmware.bin`

### JSON статус (`/status`)
```json
{"temp":23.4,"hum":61.0,"fan":2,"mode":"DAY"}
```

## Логування

### Формат запису
```
2026-04-01 21:33:23 50,23.0,62.1,3,0,DAY,FAN_CHANGE:CH0->CH3
```

| Поле | Опис |
|------|------|
| `2026-04-01 21:33:23` | Дата/час (NTP) або `[uptime s]` до синхронізації |
| `50` | Uptime (секунди) |
| `23.0` | Температура (°C) |
| `62.1` | Вологість (%) |
| `3` | Активний канал вентилятора (0–4) |
| `0` | Обігрів (0/1) |
| `DAY` / `NIGHT` | Режим роботи |
| `FAN_CHANGE:CH0->CH3` | Подія |

### Типи подій
- `BOOT` — запуск системи
- `BOOT_CYCLE` — вибір початкового циклу
- `FAN_CHANGE` — зміна каналу вентилятора
- `OVERHEAT` — перший старт при перегрітому повітрі
- `COLDLOCK` — активація/деактивація захисту від холоду
- `CYCLE_CHANGE` — зміна автоциклу (outCold/outNormal/outHot)
- `MODE_CHANGE` — перехід DAY↔NIGHT
- `NIGHT_MODE_SWITCH` — перехід TEMP_ADAPT→HUM_CTRL вночі
- `DHT_ERROR` — помилка датчика

### Очищення логів
Serial Monitor → надіслати символ `C`

### Ротація
Файл `/climate.log` обрізається до 50 КБ при перевищенні 100 КБ.

## Налаштування

### Пороги логіки (climate.cpp)
```cpp
COLDLOCK_TEMP_LOW     18.0°C  — поріг активації ColdLock
COLDLOCK_TEMP_HIGH    19.5°C  — поріг деактивації ColdLock
NIGHT_TEMP_OFFSET      4.0°C  — зсув цільової температури вночі
NIGHT_TEMP_HYSTERESIS  0.5°C  — гістерезис нічного обігріву
```

### Логіка День/Ніч

**Денний режим:**
- Автоматичні цикли outCold/outNormal/outHot з динамічним offset
- 4 температурні зони з гістерезисом
- Захист від переохолодження (ColdLock при T < 18°C)

**Нічний режим (ОНОВЛЕНО v2.6.0):**

Система працює у 2 фази:

1. **Фаза охолодження (TEMP_ADAPT):**
   - Цільова температура: `set_temp_day - 4.0°C` (напр. 19°C при set_temp=23°C)
   - Поріг переходу: `цільова_T + 0.2°C` (19.2°C)
   - Якщо `T > 19.2°C` → охолоджує на CH1
   - Якщо `T ≤ 19.2°C` → **одноразовий перехід** на фазу 2

2. **Фаза вологісного контролю (HUM_CTRL):**
   - Активується при досягненні порогу
   - **Назавжди залишається до ранку** (не повертається до TEMP_ADAPT!)
   - Зміна `set_temp_day` через Blynk **не скидає** режим
   - 3 зони вологості: CH1 (низька), CH2 (середня), CH3 (висока)
```

### Пороги День/Ніч (climate.cpp)
```
lightVal > 2500  — перехід у нічний режим
lightVal < 1500  — перехід у денний режим
```

### Інтервали (main.cpp)
```
CLIMATE_CHECK_INTERVAL  10 000 мс — читання DHT та оновлення логіки
PREF_WRITE_DELAY         5 000 мс — затримка запису налаштувань у Flash
PERIODIC_INTERVAL      120 000 мс — інтервал periodical-логу (logger.h)
```

### Управління через Blynk
| Pin | Функція | Напрямок |
|-----|---------|----------|
| V0 | Manual Boost (CH4) | вхід |
| V1 | Температура | вихід |
| V2 | Вологість | вихід |
| V3 | Активний канал | вихід |
| V4 | Стан обігріву | вихід |
| V5 | Цільова температура | вхід |
| V6 | Цільова вологість | вхід |
| V7 | День (1/0) | вихід |
| V8 | Ніч (1/0) | вихід |
| V9 | ColdLock активний | вихід |
| V10 | Система ON/OFF | вхід |
| V11 | Помилка DHT | вихід |
| V11 | humHys Гістерезис вологості | вхід |
| V13 | Offset температури (×10) | вхід |
| V14 | Offset відображення | вихід |
| V15 | Гістерезис (×10) | вхід |

## Діагностика

### Boot Diagnostics (Serial)
При кожному завантаженні виводить:
- Лічильник перезавантажень
- Попередній uptime
- Останні T/H, канал, offset, помилки DHT

### Скинути лічильник
Розкоментуй у `setup()`:
```cpp
resetBootDiagnostics();
```

### Debug-вивід
У `config.h` перемкни:
```cpp
#define DEBUG_CLIMATE 1   // 1 = увімкнено, 0 = вимкнено
```

## Безпека

- Авторизація за паролем для OTA та `/logs`
- `secrets.h` у `.gitignore`, не потрапляє в репо
- Паролі тільки в `secrets.h`

## Залежності

```ini
lib_deps =
    https://github.com/mathieucarbou/ESPAsyncWebServer#v3.3.15
    mathieucarbou/AsyncTCP@^3.2.14
    blynkkk/Blynk@^1.3.2
    adafruit/DHT sensor library@^1.4.6
    adafruit/Adafruit ST7735 and ST7789 Library@^1.10.4
    adafruit/Adafruit GFX Library@^1.11.9
```

### v2.6.0 (2026-04-16)
- ✅ Виправлено логіку нічних переходів TEMP_ADAPT → HUM_CTRL
- ✅ Додано прапорець `nightHumCtrlActive` для постійного стану HUM_CTRL
- ✅ Виправлено логування режиму (показувало протилежний стан)
- ✅ Додано детальні NIGHT_ADAPT та NIGHT_SWITCH повідомлення
- ✅ Зміна set_temp вночі більше не скидає режим на TEMP_ADAPT

### v2.5.0
- Повна нічна логіка з температурною адаптацією
- Вологісний контроль з 3 зонами
- ColdLock захист

## Автор

KoshykOleg — [GitHub](https://github.com/KoshykOleg)
