// Kachel „Orakel“: eine schlafende Kugel, die auf Tippen oder Schütteln eine Frage mit unnützem Wissen stellt.
// Drei Antworten zum Antippen, danach Auflösung mit Erklärung, Serie und Wochenquote.
// Tipp auf die Frage zeigt sie groß (Eisbrecher fürs Meeting). Die Fragen kommen fertig geprüft aus einem Paket.
#pragma once
#include <vector>

#include "input.h"
#include "quiz.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Oracle : public Tile {
 public:
  Oracle(Synth& synth, Store& store, uint32_t seed);
  void setPack(QuizPack* pack);   // nullptr: keine Fragen
  const char* title() const override { return "Orakel"; }
  void enter(uint32_t ms) override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != SLEEP; }
  bool wantsMotion() const override { return true; }   // Schütteln weckt
  int settingsCount() const override { return 3; }
  void settingText(int i, char* buf, int n) const override;
  bool settingTap(int i) override;

  // für den Simulator
  enum Phase { SLEEP, WAKE, ASK, ANSWERED, BIG };
  Phase phase() const { return phase_; }
  int correctSlot() const;   // Position der richtigen Antwort (0..2)

 private:
  void setPhase(Phase p) { phase_ = p; t_ = 0; }
  void nextQuestion();
  bool matches(int i) const;
  void shuffleOrder(uint32_t seed);
  void answer(int slot);
  void statsText(char* buf, int n) const;
  float rnd();
  void drawOrb(Scene& s, float scale, float glow, float alpha);
  void drawQuestion(Scene& s, float a);
  void drawAnswers(Scene& s, float a);

  Synth& synth_;
  Store& store_;
  uint32_t seed_;
  QuizPack* pack_ = nullptr;
  std::vector<uint16_t> order_;
  int pos_ = 0;

  Phase phase_ = SLEEP;
  float t_ = 0, clock_ = 0, idle_ = 0, msgT_ = 0, pokeT_ = 0;
  float sinceAsk_ = 1e9f;   // Sekunden seit der letzten Frage (für das stündliche Glimmen)
  int64_t epoch_ = 0;

  // aktuelle Frage
  Question q_{};
  int cur_ = -1;
  int slots_[3] = {0, 1, 2};   // Anzeigeposition → Antwortindex
  int chosen_ = -1;
  bool right_ = false;
  char qLines_[6][160];
  int nQ_ = 0;
  bool qSmall_ = false;
  char bigLines_[8][160];
  int nBig_ = 0;
  char eLines_[5][160];
  int nE_ = 0;

  // Einstellungen und Statistik (NVS)
  int cat_ = -1, diff_ = -1;   // -1 = alle
  bool nudge_ = false;         // stündlich „Ich hätte da was“
  int streak_ = 0, best_ = 0, wkId_ = -1, wkRight_ = 0, wkAll_ = 0, allRight_ = 0, all_ = 0;
};

}  // namespace dk
