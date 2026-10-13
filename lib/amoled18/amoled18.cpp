#include "amoled18.h"

#include <Wire.h>

#include "es8311.h"

namespace amoled18 {

#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS 12
#define I2S_MCK 16
#define I2S_BCK 9
#define I2S_DI 10
#define I2S_WS 45
#define I2S_DO 8

static Arduino_DataBus* bus_ = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_CO5300* panel_ = new Arduino_CO5300(bus_, GFX_NOT_DEFINED, 0, LCD_W, LCD_H, 16, 0, 0, 0);   // Spaltenversatz 16
static I2SClass i2s_;
static bool imuOk_ = false;

Arduino_DataBus* bus() { return bus_; }
Arduino_CO5300* panel() { return panel_; }
I2SClass& i2s() { return i2s_; }
bool imuOk() { return imuOk_; }

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool begin() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  // IO-Expander: Display, Touch und Co. aus dem Reset holen
  i2cWriteReg(0x20, 0x03, 0xF8);   // P0..P2 als Ausgang
  i2cWriteReg(0x20, 0x01, 0x00);
  delay(20);
  i2cWriteReg(0x20, 0x01, 0x07);
  delay(60);

  bool ok = panel_->begin(80000000);
  panel_->fillScreen(0x0000);
  panel_->setBrightness(0);

  i2cWriteReg(0x15, 0xFE, 0x01);   // Touch: nicht automatisch schlafen

  // IMU: Softreset (sonst liefert sie alte Werte), dann ±8 g, 1 kHz, Tiefpass 13,37 % ODR
  uint8_t who = 0;
  i2cWriteReg(0x6B, 0x60, 0xB0);
  delay(20);
  if (i2cRead(0x6B, 0x00, &who, 1) && who == 0x05) {
    i2cWriteReg(0x6B, 0x02, 0x40);   // CTRL1: Adressen automatisch weiterzählen, Little Endian
    i2cWriteReg(0x6B, 0x03, 0x23);   // CTRL2: ±8 g, 1000 Hz
    i2cWriteReg(0x6B, 0x06, 0x07);   // CTRL5: Beschleunigungs-Tiefpass an, Modus 3
    i2cWriteReg(0x6B, 0x08, 0x01);   // CTRL7: Beschleunigung an
    delay(30);
    imuOk_ = true;
  }
  return ok;
}

bool readTouch(bool& down, int16_t& x, int16_t& y) {
  uint8_t t[5];
  if (!i2cRead(0x15, 0x02, t, 5)) return false;
  down = (t[0] & 0x0F) > 0;
  if (down) {
    x = ((t[1] & 0x0F) << 8) | t[2];
    y = ((t[3] & 0x0F) << 8) | t[4];
  }
  return true;
}

bool readAccel(float v[3]) {
  uint8_t d[6];
  if (!imuOk_ || !i2cRead(0x6B, 0x35, d, 6)) return false;
  for (int i = 0; i < 3; i++) v[i] = int16_t(d[2 * i] | (d[2 * i + 1] << 8)) / 4096.f;   // ±8 g
  return true;
}

bool audioBegin(uint32_t rate, int volume) {
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, HIGH);
  i2s_.setPins(I2S_BCK, I2S_WS, I2S_DO, I2S_DI, I2S_MCK);
  if (!i2s_.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) return false;
  const es8311_clock_config_t clk = {false, false, true, int(rate * 256), int(rate)};
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_voice_volume_set(es, volume, nullptr);
  es8311_microphone_config(es, false);              // analoges Mikrofon
  es8311_microphone_gain_set(es, ES8311_MIC_GAIN_24DB);
  return true;
}

}  // namespace amoled18
