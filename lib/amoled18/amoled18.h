// Board-Schicht für das Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2).
// Display CO5300 (QSPI), Touch CST816 (I²C 0x15), IMU QMI8658 (0x6B), IO-Expander TCA9554 (0x20), Codec ES8311 (0x18).
// Init-Sequenzen aus Hallen-Legenden Mini übernommen (dort auf der Hardware erprobt).
#pragma once
#include <Arduino.h>
#include <ESP_I2S.h>

#include "Arduino_GFX_Library.h"

namespace amoled18 {

constexpr int LCD_W = 368, LCD_H = 448;
constexpr int PIN_SDA = 15, PIN_SCL = 14, PIN_BOOT = 0, PIN_PA_EN = 46;

// I²C, IO-Expander, Display (schwarz, Helligkeit 0), Touch (ohne Auto-Schlaf), IMU
bool begin();

Arduino_DataBus* bus();
Arduino_CO5300* panel();

// Touch: Display-Pixel. false bei I²C-Fehler.
bool readTouch(bool& down, int16_t& x, int16_t& y);

// IMU: Beschleunigung in g in Sensorachsen, ±8 g, 1 kHz, Tiefpass ~130 Hz
bool imuOk();
bool readAccel(float v[3]);

// Lautsprecher und Mikrofon: I2S + ES8311 + Verstärker. Danach mit i2s().write() Stereo-16-Bit schreiben,
// mit i2s().readBytes() das Mikrofon lesen (Stereo-16-Bit, das Mikrofon liegt auf dem linken Kanal).
bool audioBegin(uint32_t rate, int volume);
I2SClass& i2s();

inline bool bootPressed() { return digitalRead(PIN_BOOT) == LOW; }

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val);
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n);

}  // namespace amoled18
