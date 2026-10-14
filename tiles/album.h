// Kachel „Sammelalbum“: Sammelkarten mit Foto und Infos. Das Thema (Pferde, Dinos, …) legen die Eltern im Portal fest;
// eingebaut sind die Pferde. Alle drei Sterne aus den Übungen gibt es eine neue Karte (verdeckt, „Tippen zum Aufdecken“),
// nie doppelt. Vorderseite: Foto, Name, Untertitel, zwei Zeilen. Ein Tipp dreht um: zwei Abschnitte, „Wusstest du?“, Bildnachweis.
//
// Kartentext (eine Karte pro Zeile, | als Trenner), Kopfzeilen mit #:
//   #album <id>            #titel Dino-Album      #symbol Dinos (Name auf dem Dashboard)
//   #zaehlwort Dinos       („3 von 24 Dinos“)     #frage Welcher Dino ist das? (Bonusfrage im Rätsel)
//   #format 2
//   id|name|untertitel|zeile1|zeile2|überschrift3|text3|überschrift4|text4|wusstest|bildnachweis
// Ohne „#format 2“ gilt das Pferdeformat: id|name|typ|herkunft|größe|farben|wesen|besonders|foto
// Jedes Thema hat einen eigenen Sammelstand (NVS); neue Themen beginnen bei null Karten.
#pragma once
#include <vector>

#include "images.h"
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

struct Card {
  int id;
  const char *name, *sub, *line1, *label2, *line2, *head3, *text3, *head4, *text4, *fact, *credit;
};

class Album : public Tile {
 public:
  static constexpr int STARS_PER_CARD = 3, MAX_CARDS = 48;

  Album(Synth& synth, Store& store, uint32_t seed);
  void setImages(ImageStore* img) { img_ = img; }
  bool load(const char* text);   // Kartentext, wird kopiert
  const char* title() const override { return title_; }
  void enter(uint32_t ms) override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != GRID; }

  int count() const { return int(cards_.size()); }
  int owned() const { return opened_; }
  int pending() const;
  int ownedCard(int k) const { return order_[k]; }   // k-te gesammelte Karte (k < owned()) → Kartenindex
  const Card& cardInfo(int i) const { return cards_[i]; }
  const char* theme() const { return id_; }
  const char* shortName() const { return short_; }  // Name auf dem Dashboard
  const char* question() const { return question_; } // Bonusfrage im Rätsel
  bool horses() const { return horses_; }           // eingebaute Pferde: gezeichnete Pferde als Platzhalter

  // für den Simulator
  enum Phase { GRID, FRONT, BACK, WRAPPED, REVEAL };
  Phase phase() const { return phase_; }
  static void cellRect(int slot, float& x, float& y, float& w, float& h);

 private:
  void shuffle();
  void drawGrid(Scene& s);
  void drawFront(Scene& s, int card, float a);
  void drawBack(Scene& s, int card, float a);
  void drawWrapped(Scene& s, float a);
  void drawPlaceholder(Scene& s, float x, float y, float scale, bool locked, float a);
  bool isOwned(int i) const;

  Synth& synth_;
  Store& store_;
  ImageStore* img_ = nullptr;
  uint32_t seed_;
  std::vector<char> text_;
  std::vector<Card> cards_;
  std::vector<int> order_;   // gemischte Reihenfolge, in der die Karten freigeschaltet werden
  int opened_ = 0, base_ = 0;
  char id_[24] = "", title_[32] = "", short_[16] = "", count_[16] = "", question_[48] = "";
  char kOpen_[12] = "", kSeed_[12] = "", kBase_[12] = "";   // NVS-Schlüssel je Thema
  bool horses_ = false;
  Phase phase_ = GRID;
  int page_ = 0, card_ = 0;
  float t_ = 0, clock_ = 0, hintT_ = 0;
};

}  // namespace dk
