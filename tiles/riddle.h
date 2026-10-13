// Kachel „Rätsel“ für die 2. Klasse: eine kurze Runde aus 5 Fragen (Rechnen, Zählen mit Bild, Tiere, Zeit, Sprache …)
// und, sobald drei Pferdekarten gesammelt sind, einer Bonusfrage „Welches Pferd ist das?“ mit Foto aus dem Album.
// Ohne Zeitdruck: Richtig geht es von allein weiter, bei falsch zeigt Grummel freundlich die Lösung und wartet aufs Tippen.
// Am Ende gibt es einen Stern (für die Runde, nicht für die Zahl der richtigen Antworten).
#pragma once
#include "album.h"
#include "images.h"
#include "input.h"
#include "quiz.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Riddle : public Tile {
 public:
  static constexpr int PER_ROUND = 5;

  Riddle(Synth& synth, Store& store, uint32_t seed) : synth_(synth), store_(store), seed_(seed | 1) {}
  void setPack(QuizPack* pack) { pack_ = pack; }
  void setAlbum(const Album* album, ImageStore* img) { album_ = album; img_ = img; }
  const char* title() const override { return "Rätsel"; }
  void enter(uint32_t) override { phase_ = IDLE; t_ = 0; }
  void arrive(bool returning) override { if (!returning) start(); }
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != IDLE; }

  enum Phase { IDLE, ASK, RIGHT, WRONG, DONE };
  enum Kind { TEXT, COUNT, HORSE };
  // für den Simulator
  Phase phase() const { return phase_; }
  Kind kind() const { return kind_; }
  int correct() const { return correct_; }
  void answerRect(int i, float& x, float& y, float& w, float& h) const;
  void countPos(int i, float& x, float& y) const;
  int countN() const { return countN_; }

 private:
  void start();
  void next();
  bool pickQuestion();
  bool pickHorse();
  void answer(int i);
  float rnd();

  Synth& synth_;
  Store& store_;
  QuizPack* pack_ = nullptr;
  const Album* album_ = nullptr;
  ImageStore* img_ = nullptr;
  uint32_t seed_;
  Phase phase_ = IDLE;
  float t_ = 0, clock_ = 0;
  int qNo_ = 0, nQ_ = PER_ROUND, solved_ = 0, stars_ = 0;
  // aktuelle Frage (Texte kopiert, weil QuizPack::get einen gemeinsamen Puffer nutzt)
  Kind kind_ = TEXT;
  char q_[120] = "", a_[3][40] = {}, expl_[180] = "";
  int correct_ = 0, chosen_ = -1, horse_ = -1;
  int countN_ = 0, countWhat_ = 0;   // Zählbild: Anzahl, 0 Apfel 1 Stern 2 Herz 3 Ball
  uint16_t counted_ = 0;             // angetippte Dinge (Zählhilfe)
  int lastCat_ = -1;
  int recent_[16];
  int nRecent_ = 0;
};

}  // namespace dk
