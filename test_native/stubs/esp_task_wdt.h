#pragma once
inline void esp_task_wdt_reset() {}
inline int esp_task_wdt_init(int, bool) { return 0; }
inline int esp_task_wdt_add(void*) { return 0; }
