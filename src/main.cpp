/**
 * ESP32-Config-Vault
 *
 * سیستم مدیریت تنظیمات دائمی با NVS
 * ویژگی‌ها:
 *   - ذخیره‌ی تنظیمات WiFi، MQTT، و کاربر در NVS
 *   - Provisioning Flow: اگه پیکربندی نشده → AP Mode
 *   - Runtime Config Update از طریق Serial Commands
 *   - Factory Reset با Long Press روی BOOT
 *   - Boot Analysis: شمارش ری‌استارت و Crash
 *   - Thread-Safe NVS با Mutex (آماده برای FreeRTOS)
 *   - آمار و Health Check حافظه
 *
 * سخت‌افزار: فقط ESP32 (بدون هیچ قطعه‌ی خارجی)
 */

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

// ==================== تعریف‌های ثابت ====================
// ⚠️ همیشه از #define برای کلیدهای NVS استفاده کن
// تا از خطای بیش از ۱۵ کاراکتر جلوگیری بشه

// Namespaces (max 15 chars)
#define NS_PROVISION  "prov"
#define NS_USER_CFG   "usr-cfg"
#define NS_SYS_LOG    "sys-log"

// Keys - Provision (max 15 chars)
#define KEY_WIFI_SSID "ssid"
#define KEY_WIFI_PASS "pass"
#define KEY_MQTT_HOST "mqtt_host"
#define KEY_MQTT_PORT "mqtt_port"
#define KEY_PROV_TIME "prov_ts"

// Keys - User Config
#define KEY_BRIGHTNESS  "brightness"
#define KEY_DARK_MODE   "dark_mode"
#define KEY_LANGUAGE    "language"
#define KEY_SLEEP_S     "sleep_s"
#define KEY_SENS_INT    "sens_int"

// Keys - System Log
#define KEY_BOOT_COUNT  "boot_cnt"
#define KEY_CRASH_COUNT "crash_cnt"
#define KEY_LAST_RESET  "last_reset"

// GPIO
#define BUTTON_PIN      0
#define LED_PIN         2
#define LONG_PRESS_MS   3000

// ==================== ساختارهای داده ====================

struct ProvisionConfig {
    char wifiSSID[32]   = "";
    char wifiPass[64]   = "";
    char mqttHost[64]   = "broker.hivemq.com";
    uint16_t mqttPort   = 1883;
    bool isProvisioned  = false;
};

struct UserConfig {
    uint8_t brightness   = 70;
    bool darkMode        = false;
    char language[8]     = "fa";
    uint16_t sleepAfter  = 300;   // seconds
    uint32_t sensorInt   = 5000;  // milliseconds
};

struct SystemLog {
    uint32_t bootCount  = 0;
    uint32_t crashCount = 0;
    uint8_t  lastReset  = 0;  // esp_reset_reason_t
};

// ==================== متغیرهای سراسری ====================
ProvisionConfig gProvConfig;
UserConfig      gUserConfig;
SystemLog       gSysLog;
SemaphoreHandle_t nvsMutex = nullptr;

// ==================== ConfigManager ====================

class ConfigManager {
public:
    static bool init() {
        nvsMutex = xSemaphoreCreateMutex();
        return nvsMutex != nullptr;
    }

    // ---- Provision ----
    static bool loadProvision() {
        bool ok = false;
        if (_takeMutex()) {
            Preferences p;
            p.begin(NS_PROVISION, true);
            gProvConfig.isProvisioned = p.isKey(KEY_WIFI_SSID);
            if (gProvConfig.isProvisioned) {
                p.getString(KEY_WIFI_SSID, "").toCharArray(gProvConfig.wifiSSID, 32);
                p.getString(KEY_WIFI_PASS, "").toCharArray(gProvConfig.wifiPass, 64);
                p.getString(KEY_MQTT_HOST, "broker.hivemq.com").toCharArray(gProvConfig.mqttHost, 64);
                gProvConfig.mqttPort = p.getUShort(KEY_MQTT_PORT, 1883);
            }
            p.end();
            _giveMutex();
            ok = gProvConfig.isProvisioned;
        }
        return ok;
    }

    static bool saveProvision(const char* ssid, const char* pass, const char* mqttHost = "broker.hivemq.com", uint16_t mqttPort = 1883) {
        bool ok = false;
        if (_takeMutex()) {
            Preferences p;
            p.begin(NS_PROVISION, false);
            ok = (p.putString(KEY_WIFI_SSID, ssid) > 0);
            ok &= (p.putString(KEY_WIFI_PASS, pass) > 0);
            ok &= (p.putString(KEY_MQTT_HOST, mqttHost) > 0);
            ok &= (p.putUShort(KEY_MQTT_PORT, mqttPort) > 0);
            p.putULong(KEY_PROV_TIME, millis());
            p.end();
            _giveMutex();

            if (ok) loadProvision(); // بارگذاری دوباره در RAM
        }
        return ok;
    }

    // ---- User Config ----
    static void loadUserConfig() {
        if (_takeMutex()) {
            Preferences p;
            p.begin(NS_USER_CFG, true);
            gUserConfig.brightness = p.getUChar(KEY_BRIGHTNESS, 70);
            gUserConfig.darkMode   = p.getBool(KEY_DARK_MODE, false);
            p.getString(KEY_LANGUAGE, "fa").toCharArray(gUserConfig.language, 8);
            gUserConfig.sleepAfter = p.getUShort(KEY_SLEEP_S, 300);
            gUserConfig.sensorInt  = p.getUInt(KEY_SENS_INT, 5000);
            p.end();
            _giveMutex();
        }
    }

    static bool saveUserConfig() {
        bool ok = false;
        if (_takeMutex()) {
            Preferences p;
            p.begin(NS_USER_CFG, false);
            ok  = (p.putUChar(KEY_BRIGHTNESS, gUserConfig.brightness) > 0);
            ok &= (p.putBool(KEY_DARK_MODE, gUserConfig.darkMode));
            ok &= (p.putString(KEY_LANGUAGE, gUserConfig.language) > 0);
            ok &= (p.putUShort(KEY_SLEEP_S, gUserConfig.sleepAfter) > 0);
            ok &= (p.putUInt(KEY_SENS_INT, gUserConfig.sensorInt) > 0);
            p.end();
            _giveMutex();
        }
        return ok;
    }

    // ---- System Log ----
    static void loadAndUpdateSysLog() {
        if (_takeMutex()) {
            Preferences p;
            p.begin(NS_SYS_LOG, false);

            gSysLog.bootCount  = p.getUInt(KEY_BOOT_COUNT, 0) + 1;
            gSysLog.crashCount = p.getUInt(KEY_CRASH_COUNT, 0);
            gSysLog.lastReset  = (uint8_t)esp_reset_reason();

            // اگه علت ری‌استارت crash بود، counter رو بالا ببر
            if (gSysLog.lastReset == ESP_RST_PANIC ||
                gSysLog.lastReset == ESP_RST_WDT) {
                gSysLog.crashCount++;
                p.putUInt(KEY_CRASH_COUNT, gSysLog.crashCount);
            }

            p.putUInt(KEY_BOOT_COUNT, gSysLog.bootCount);
            p.putUChar(KEY_LAST_RESET, gSysLog.lastReset);
            p.end();
            _giveMutex();
        }
    }

    // ---- Factory Reset ----
    static void factoryReset() {
        Serial.println("⚠️  FACTORY RESET IN PROGRESS...");
        if (_takeMutex()) {
            Preferences p;

            p.begin(NS_PROVISION, false); p.clear(); p.end();
            p.begin(NS_USER_CFG,  false); p.clear(); p.end();
            // SysLog رو نگه می‌داریم (بوت‌شمار و کرش‌شمار)

            _giveMutex();
        }
        Serial.println("✅ Factory reset complete. Rebooting in 2s...");
        delay(2000);
        ESP.restart();
    }

    // ---- Health Check ----
    static void printHealthReport() {
        Preferences p;

        Serial.println("\n╔══════════════════════════════════╗");
        Serial.println("║       NVS Health Report           ║");
        Serial.println("╠══════════════════════════════════╣");
        Serial.printf( "║ Boot Count:   %-20u║\n", gSysLog.bootCount);
        Serial.printf( "║ Crash Count:  %-20u║\n", gSysLog.crashCount);
        Serial.printf( "║ Last Reset:   %-20d║\n", gSysLog.lastReset);
        Serial.println("╠══════════════════════════════════╣");
        Serial.printf( "║ Free Heap:    %-16d bytes║\n", xPortGetFreeHeapSize());
        Serial.printf( "║ Min Heap:     %-16d bytes║\n", xPortGetMinimumEverFreeHeapSize());

        if (nvsMutex && xSemaphoreTake(nvsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            p.begin(NS_PROVISION, true);
            Serial.printf("║ NVS Free:     %-16zu slots║\n", p.freeEntries());
            p.end();
            xSemaphoreGive(nvsMutex);
        }

        Serial.println("╠══════════════════════════════════╣");
        Serial.printf( "║ WiFi SSID:    %-20s║\n",
                       gProvConfig.isProvisioned ? gProvConfig.wifiSSID : "(not set)");
        Serial.printf( "║ MQTT Host:    %-20s║\n", gProvConfig.mqttHost);
        Serial.printf( "║ MQTT Port:    %-20u║\n", gProvConfig.mqttPort);
        Serial.println("╠══════════════════════════════════╣");
        Serial.printf( "║ Brightness:   %-20u║\n", gUserConfig.brightness);
        Serial.printf( "║ Dark Mode:    %-20s║\n", gUserConfig.darkMode ? "ON" : "OFF");
        Serial.printf( "║ Language:     %-20s║\n", gUserConfig.language);
        Serial.printf( "║ Sensor Int:   %-16u ms║\n", gUserConfig.sensorInt);
        Serial.println("╚══════════════════════════════════╝\n");
    }

private:
    static bool _takeMutex() {
        if (nvsMutex == nullptr) return true; // قبل از init FreeRTOS
        return xSemaphoreTake(nvsMutex, pdMS_TO_TICKS(2000)) == pdTRUE;
    }
    static void _giveMutex() {
        if (nvsMutex) xSemaphoreGive(nvsMutex);
    }
};

// ==================== Serial Command Handler ====================

void handleSerialCommands() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    Serial.printf("\n> Command: '%s'\n", cmd.c_str());
    
    if (cmd == "help") {
        Serial.println("Commands:");
        Serial.println("  status       - Show all config & health");
        Serial.println("  dark on/off  - Toggle dark mode");
        Serial.println("  bright [0-100] - Set brightness");
        Serial.println("  interval [ms]  - Set sensor interval");
        Serial.println("  lang [fa/en]   - Set language");
        Serial.println("  reset          - Factory reset");
        Serial.println("  setprov        - Set dummy provision data");
    }
    else if (cmd == "status") {
        ConfigManager::printHealthReport();
    }
    else if (cmd == "dark on") {
        gUserConfig.darkMode = true;
        ConfigManager::saveUserConfig();
        Serial.println("Dark mode: ON (saved)");
    }
    else if (cmd == "dark off") {
        gUserConfig.darkMode = false;
        ConfigManager::saveUserConfig();
        Serial.println("Dark mode: OFF (saved)");
    }
    else if (cmd.startsWith("bright ")) {
        int val = cmd.substring(7).toInt();
        if (val >= 0 && val <= 100) {
            gUserConfig.brightness = (uint8_t)val;
            ConfigManager::saveUserConfig();
            Serial.printf("Brightness: %d%% (saved)\n", val);
        } else {
            Serial.println("Invalid value. Use 0-100.");
        }
    }
    else if (cmd.startsWith("interval ")) {
        uint32_t val = cmd.substring(9).toInt();
        if (val >= 100 && val <= 60000) {
            gUserConfig.sensorInt = val;
            ConfigManager::saveUserConfig();
            Serial.printf("Sensor interval: %ums (saved)\n", val);
        } else {
            Serial.println("Invalid. Use 100-60000 ms.");
        }
    }
    else if (cmd.startsWith("lang ")) {
        String lang = cmd.substring(5);
        if (lang == "fa" || lang == "en") {
            lang.toCharArray(gUserConfig.language, 8);
            ConfigManager::saveUserConfig();
            Serial.printf("Language: %s (saved)\n", lang.c_str());
        } else {
            Serial.println("Invalid. Use 'fa' or 'en'.");
        }
    }
    else if (cmd == "reset") {
        ConfigManager::factoryReset();
    }
    else if (cmd == "setprov") {
        // برای تست — شبیه‌سازی provisioning
        ConfigManager::saveProvision("TestWiFi", "TestPassword", "broker.hivemq.com", 1883);
        Serial.println("Dummy provision data saved!");
    }
    else {
        Serial.println("Unknown command. Type 'help'.");
    }
}

// ==================== Button Handler ====================

unsigned long buttonPressStart = 0;
bool buttonPressed = false;

void handleButton() {
    bool isPressed = (digitalRead(BUTTON_PIN) == LOW);
    
    if (isPressed && !buttonPressed) {
        // شروع فشار
        buttonPressed = true;
        buttonPressStart = millis();
    }
    else if (!isPressed && buttonPressed) {
        // رها شد
        uint32_t held = millis() - buttonPressStart;
        buttonPressed = false;
        
        if (held >= LONG_PRESS_MS) {
            ConfigManager::factoryReset();
        } else if (held > 50) {
            // Short press — toggle dark mode
            gUserConfig.darkMode = !gUserConfig.darkMode;
            ConfigManager::saveUserConfig();
            digitalWrite(LED_PIN, gUserConfig.darkMode ? HIGH : LOW);
            Serial.printf("Short press: Dark mode %s\n",
                          gUserConfig.darkMode ? "ON" : "OFF");
        }
    }
    else if (isPressed && buttonPressed) {
        // در حال نگه داشتن
        uint32_t held = millis() - buttonPressStart;
        if (held > LONG_PRESS_MS / 2) {
            // چشمک LED هشدار
            digitalWrite(LED_PIN, (millis() / 200) % 2);
        }
    }
}

// ==================== Main App ====================

unsigned long lastSensorTime = 0;
unsigned long lastReportTime = 0;

void runMainApp() {
    uint32_t now = millis();
    
    // شبیه‌سازی خواندن سنسور
    if (now - lastSensorTime >= gUserConfig.sensorInt) {
        lastSensorTime = now;
        float fakeTemp = 20.0f + (random(0, 100) / 10.0f);
        Serial.printf("[SENSOR] Temp: %.1f°C\n", fakeTemp);
    }
    
    // گزارش هر ۳۰ ثانیه
    if (now - lastReportTime >= 30000) {
        lastReportTime = now;
        ConfigManager::printHealthReport();
    }
}

// ==================== setup & loop ====================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔══════════════════════════════════╗");
    Serial.println("║     ESP32-Config-Vault v1.0       ║");
    Serial.println("╚══════════════════════════════════╝");
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    
    // مقداردهی ConfigManager
    ConfigManager::init();
    
    // بارگذاری لاگ سیستم + شمارش boot
    ConfigManager::loadAndUpdateSysLog();
    
    // نمایش علت ری‌استارت
    const char* resetReasons[] = {
        "Unknown", "Power on", "External pin", "Software",
        "Exception/Panic", "Interrupt WDT", "Task WDT", "WDT",
        "Deep Sleep", "Brownout", "SDIO"
    };
    uint8_t reason = gSysLog.lastReset;
    Serial.printf("Boot #%u | Reset reason: %s\n",
                  gSysLog.bootCount,
                  reason < 11 ? resetReasons[reason] : "Other");
    
    // بارگذاری تنظیمات
    bool provisioned = ConfigManager::loadProvision();
    ConfigManager::loadUserConfig();
    
    if (!provisioned) {
        Serial.println("\n⚠️  Device not provisioned!");
        Serial.println("   In a real product, AP Mode would start here.");
        Serial.println("   Type 'setprov' to simulate provisioning.\n");
    } else {
        Serial.printf("✅ Provisioned for: %s\n\n", gProvConfig.wifiSSID);
    }
    
    ConfigManager::printHealthReport();
    
    Serial.println("Type 'help' for available commands.");
    Serial.println("Hold BOOT button 3s for factory reset.");
    Serial.println("Short press BOOT to toggle dark mode.\n");
    
    digitalWrite(LED_PIN, gUserConfig.darkMode ? HIGH : LOW);
}

void loop() {
    handleButton();
    handleSerialCommands();
    runMainApp();
    delay(20);
}