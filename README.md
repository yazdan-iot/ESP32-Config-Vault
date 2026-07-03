# 🗄️ ESP32-Config-Vault

**A persistent configuration management system for ESP32, built entirely around NVS (Non-Volatile Storage)**

A single-file project (`main.cpp`) that demonstrates how to use **NVS** the way it's actually used in production firmware — safely, cleanly, and thread-safely — using nothing but an ESP32 board and its onboard BOOT button. No external components required.

---

## 📖 What Is This Project?

Most NVS tutorials stop at a simple `putInt` / `getInt` example. This project goes a step further and implements **real-world, production-style NVS patterns** commonly found in commercial embedded products:

- Separating configuration into multiple isolated namespaces
- Managing a provisioning state ("configured" vs "not configured")
- Updating settings at runtime via Serial commands
- Performing a factory reset while preserving system logs
- Tracking reboot causes and crash counts across power cycles
- Accessing NVS safely from a thread-safe context using a Mutex (FreeRTOS-ready)
- Reporting memory health and free NVS storage slots

The goal of this project is to **demonstrate solid, practical knowledge of NVS** on ESP32 — not to build a finished consumer product. That's why the hardware is intentionally minimal (just the onboard BOOT button and LED).

---

## ✨ Key Features

### 🔑 Multi-Namespace Configuration Management
Settings are split into three isolated namespaces for clarity and so each can be reset independently:

| Namespace | Responsibility |
|---|---|
| `prov` | Provisioning data (WiFi, MQTT) |
| `usr-cfg` | User preferences (brightness, dark mode, language, ...) |
| `sys-log` | System log (boot count, crash count, last reset reason) |

### 🌐 Provisioning Flow
On boot, the device checks whether it has already been provisioned (SSID stored, etc.). If not, it prints a warning (in a real product, AP Mode would start here).

### ⚙️ Runtime Configuration via Serial
Simple text commands let you update values and persist them to NVS instantly:
```
status              Show full status and health report
dark on / dark off  Toggle dark mode
bright [0-100]      Set brightness
interval [ms]       Set sensor read interval
lang [fa/en]        Change language
reset               Factory reset
setprov             Simulate provisioning for testing
```

### 🔘 BOOT Button Handling
- **Short press** → toggles Dark Mode
- **Long press (3s)** → triggers Factory Reset (LED blinks as a warning before the reset happens)

### 🩺 Boot Analysis & Crash Tracking
On every boot, `esp_reset_reason()` is read. If the cause was a Panic or Watchdog Timeout, the crash counter stored in NVS is incremented — meaning crash history survives even a full power cycle.

### 🔒 Thread-Safe Access with a Mutex
All NVS read/write operations are protected with a `SemaphoreHandle_t` to avoid race conditions on flash storage in multi-task FreeRTOS environments.

### 📊 Health Report
A clean report is printed to the Serial Monitor, including:
- Boot count and crash count
- Last reset reason
- Free heap / minimum ever free heap
- Free NVS entry slots (`freeEntries()`)
- A full summary of current settings

### ♻️ Smart Factory Reset
Factory reset only clears the `prov` and `usr-cfg` namespaces; `sys-log` is deliberately preserved so the device's boot/crash history survives a reset — a design decision that shows the difference between "wiping user settings" and "wiping the entire device history."

---

## 🧠 NVS Concepts Demonstrated in This Project

- ✅ Using `Preferences.h` (the Arduino-level abstraction over raw NVS) instead of the raw `nvs_flash` API, with proper `begin()` / `end()` lifecycle management
- ✅ Respecting the **15-character limit** on namespace and key names (enforced via `#define` to avoid runtime errors)
- ✅ Opening NVS in **read-only** mode (`true`) for pure reads and **read-write** mode (`false`) for writes, to reduce flash wear and improve data safety
- ✅ Checking the return value of every `put*()` call to confirm the write actually succeeded
- ✅ Using `isKey()` to detect whether the device has already been provisioned
- ✅ Logically separating data into multiple namespaces instead of dumping everything into one
- ✅ Synchronizing all NVS access with a Mutex to prevent corruption in multi-task environments
- ✅ Monitoring free NVS space with `freeEntries()` to catch storage exhaustion before it happens

---

## 🛠️ Hardware Required

- Any **ESP32** board with an onboard BOOT button and LED
- A USB cable
- **No external components needed** 🎉

---

## 🚀 Getting Started

1. Open the project folder in **VS Code** with the **PlatformIO IDE** extension installed.
2. `platformio.ini` already defines the board and framework — the `Preferences.h`, `WiFi.h`, and `esp_system.h` libraries ship by default with the ESP32 Arduino core, so no extra dependencies are needed.
3. Build and upload with the PlatformIO toolbar (or `pio run --target upload`).
4. Open the Serial Monitor at `115200` baud (or `pio device monitor`).
5. Type `help` to see the full list of available commands.

---

## 📂 Project Structure

This is a standard **PlatformIO** project:

```
ESP32-Config-Vault/
├── .pio/               ← PlatformIO build files (auto-generated, git-ignored)
├── .vscode/             ← VS Code / PlatformIO IDE settings
├── include/             ← Project header files
├── lib/                 ← Private project libraries
├── src/
│   └── main.cpp         ← Entire application logic (config structs, ConfigManager, serial commands, button/main loop)
├── test/                ← Unit tests
├── .gitignore
└── platformio.ini       ← PlatformIO project configuration (board, framework, dependencies)
```

---

## 💡 Why I Built This

This project is a portfolio piece meant to demonstrate that I:

- Have a solid understanding of **NVS and the data lifecycle on ESP32 flash storage**
- Can design a **clean, modular architecture** for configuration management (the `ConfigManager` class acts as an abstraction layer over NVS)
- Pay attention to details that are often overlooked in tutorial-level code — **key length limits, flash wear leveling, thread safety, and proper error handling**

Feedback, questions, and suggestions are always welcome! 🙌

---

## 📜 License

Released for educational purposes and as a skills demonstration. Feel free to use, modify, and learn from it. 🚀
