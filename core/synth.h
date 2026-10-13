// Kleiner Synthesizer (Engine aus Hallen-Legenden Mini): Stimmen mit Wellenform, Filter und Hüllkurve, alles zur Laufzeit.
// render() läuft im Audio-Thread der Plattform, play() im App-Thread; ein Mutex trennt beide.
#pragma once
#include <cstdint>
#include <mutex>

namespace dk {

class Synth {
 public:
  static constexpr int RATE = 16000;

  enum Wave : uint8_t { SINE, SQUARE, TRI, SAW, NOISE };
  enum Filt : uint8_t { NONE, LP, BP, HP };
  struct Voice {
    Wave wave = SINE;
    Filt filt = NONE;
    float f0 = 440, f1 = 440;     // Frequenz, exponentiell von f0 nach f1
    float cut0 = 1000, cut1 = 1000, q = 0.7f;
    float vol = 0.3f, attack = 0.005f, dur = 0.2f, delay = 0;
    bool hold = false;            // true: flach halten und am Ende kurz ausblenden, sonst exponentiell abklingen
    float lfoF = 0, lfoD = 0;     // Vibrato (Hz, Tiefe in Hz)
    float amF = 0, amD = 0;       // Lautstärke-Schwankung
  };

  void render(int16_t* out, int n);   // Mono, 16 Bit
  void play(const Voice& v);
  void setVolume(float v) { std::lock_guard<std::mutex> l(m_); master_ = v; }
  float volume() const { return master_; }
  uint32_t rnd();

 private:
  struct Slot {
    Voice v;
    bool on = false;
    float t = 0, ph = 0, lp = 0, bp = 0;
  };
  float sample(Slot& s, float dt);

  std::mutex m_;
  Slot s_[14];
  float master_ = 0;
  uint32_t seed_ = 0x1234567;
};

}  // namespace dk
