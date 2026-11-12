#include "diag.h"

#if TAMAPOKE_LOG > 0

#include <Arduino.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

static bool gOn = true;
static uint32_t gFrames = 0;
static uint32_t gFrameMark = 0;
static uint32_t gFrameCountAtMark = 0;

static const char *resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "pwr-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    default: return "unknown";
  }
}

static void printCoredump() {
  esp_err_t chk = esp_core_dump_image_check();
  if (chk != ESP_OK) {
    Serial.println("coredump: none (partition coredump @ 0x1FF0000 is empty or unchecked)");
    return;
  }
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) == ESP_OK)
    Serial.printf("coredump: flash 0x%x size %u (kept; esptool read_flash 0x1FF0000 0x10000 coredump.bin)\n",
                  (unsigned)addr, (unsigned)size);
  char reason[160];
  if (esp_core_dump_get_panic_reason(reason, sizeof(reason)) == ESP_OK)
    Serial.printf("coredump panic: %s\n", reason);
  esp_core_dump_summary_t sum;
  if (esp_core_dump_get_summary(&sum) == ESP_OK) {
    Serial.printf("coredump task=%s pc=0x%08x cause=%u vaddr=0x%08x depth=%u%s\n",
                  sum.exc_task, sum.exc_pc, sum.ex_info.exc_cause, sum.ex_info.exc_vaddr,
                  sum.exc_bt_info.depth, sum.exc_bt_info.corrupted ? " CORRUPT" : "");
    Serial.print("coredump bt:");
    uint32_t n = sum.exc_bt_info.depth;
    if (n > 16) n = 16;
    for (uint32_t i = 0; i < n; i++) Serial.printf(" 0x%08x", sum.exc_bt_info.bt[i]);
    Serial.println();
  }
  // Leave the image in flash so it can be pulled with esptool after this log.
}

void diagBoot() {
  Preferences p;
  p.begin("tamapoke", false);
  uint32_t boots = p.getUInt("boots", 0) + 1;
  p.putUInt("boots", boots);
  gOn = p.getBool("log", true);
  p.end();
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("boot %u reset=%s log=%d level=%d\n", boots, resetName(reason), gOn ? 1 : 0, TAMAPOKE_LOG);
  if (reason == ESP_RST_BROWNOUT)
    Serial.println("brownout: supply dipped; lower brightness or charge the cell");
  printCoredump();
  gFrameMark = millis();
}

bool diagRuntimeOn() { return gOn; }

void diagSetRuntime(bool on) {
  gOn = on;
  Preferences p;
  p.begin("tamapoke", false);
  p.putBool("log", on);
  p.end();
  Serial.printf("log %s\n", on ? "on" : "off");
}

void diagError(const char *msg) {
  Serial.printf("ERR %s\n", msg);
}

void diagInfo(const char *msg) {
#if TAMAPOKE_LOG >= 2
  if (gOn) Serial.printf(".. %s\n", msg);
#else
  (void)msg;
#endif
}

void diagFeedWdt() { esp_task_wdt_reset(); }

void diagNoteFrame() { gFrames++; }

void diagHeartbeat(const DiagBeat &b) {
#if TAMAPOKE_LOG >= 2
  if (!gOn) return;
  uint32_t now = millis();
  uint32_t dt = now - gFrameMark;
  uint32_t frames = gFrames - gFrameCountAtMark;
  gFrameMark = now;
  gFrameCountAtMark = gFrames;
  unsigned fps10 = dt ? (frames * 10000UL) / dt : 0;  // tenths of a frame per second
  uint32_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t stack = uxTaskGetStackHighWaterMark(nullptr);
  Serial.printf("beat up=%lus heap=%u min=%u psram=%u stack=%u bat=%d%% %dmV chg=%d usb=%d dim=%u off=%d lock=%d fps=%u.%u\n",
                (unsigned long)(now / 1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                psram, stack, b.batPct, b.mv, b.charging ? 1 : 0, b.usb ? 1 : 0,
                b.dim, b.screenOff ? 1 : 0, b.locked ? 1 : 0, fps10 / 10, fps10 % 10);
#else
  (void)b;
#endif
}

#else

void diagBoot() {}
bool diagRuntimeOn() { return false; }
void diagSetRuntime(bool) {}
void diagError(const char *) {}
void diagInfo(const char *) {}
void diagFeedWdt() {}
void diagNoteFrame() {}
void diagHeartbeat(const DiagBeat &) {}

#endif
