// Dashboard: Grummels Zuhause. Grummel ist ein sanftes Haustier (siehe pet.h): Er schlüpft aus einem Ei,
// wächst durch Pflege und zeigt oben drei Bedürfnisse (Apfel = satt, Herz = gekuschelt, Ball = gespielt).
// Er reagiert aufs Streicheln, Antippen, Schütteln und auf Stimme (nur der Pegel) und fragt ab und zu
// „Wie geht's dir?“ (Antippen der Blase öffnet den Check-in). Darunter liegen die App-Symbole.
#pragma once
#include "album.h"
#include "messages.h"
#include "input.h"
#include "pet.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Home : public Tile {
 public:
  struct AppIcon { const char* label; int tile; int kind; };   // kind: 0 Gefühle, 1 Grummel, 2 Atmen, 3 Kugel, 4 Album, 5 Rätsel, 6 Radio, 7 Post
  Home(Synth& synth, Store& store);
  void setApps(const AppIcon* apps, int n) { apps_ = apps; nApps_ = n; nVis_ = n < 8 ? n : 8; for (int i = 0; i < nVis_; i++) vis_[i] = i; }
  void setCheckIn(int tile) { checkin_ = tile; }
  void setAlbum(const Album* a) { album_ = a; }
  void setPost(Messages* m, int tile) { msgs_ = m; postTile_ = tile; }   // „Post von …“ und Abzeichen am Post-Symbol   // Name, Symbol und „neue Karte“-Abzeichen des Sammelalbums
  const char* title() const override { return "Start"; }
  void enter(uint32_t ms) override;
  void leave() override { pet_.save(store_); }
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool wantsMotion() const override { return true; }
  bool claimsDrag() const override { return onBuddy_; }   // Streicheln öffnet keine Einstellungen

  static void iconPos(int i, int n, float& x, float& y);
  static void needPos(int i, float& x, float& y);
  static constexpr float BUDDY_X = W / 2, BUDDY_Y = 176, BUDDY_MAX = 80;   // Mitte bei voller Größe; kleiner steht er auf derselben Linie
  bool claimsDragAt(float x, float y) const override;
  float buddyY() const { return BUDDY_Y + (BUDDY_MAX - R_) * 0.8f; }
  const Pet& pet() const { return pet_; }   // für den Simulator

 private:
  void say(const char* text, float secs, bool ask = false);
  void feed();
  bool asleep() const;
  void drawBuddy(Scene& s);
  void drawEgg(Scene& s);
  void drawNeeds(Scene& s);
  void drawIcon(Scene& s, const AppIcon& a, float x, float y);
  int cardsWaiting() const;
  float rnd();

  Synth& synth_;
  Store& store_;
  const AppIcon* apps_ = nullptr;
  const Album* album_ = nullptr;
  Messages* msgs_ = nullptr;
  int postTile_ = -1, bubbleTo_ = -1;   // wohin ein Tipp auf die Sprechblase führt
  float postAskT_ = 0;
  int nApps_ = 0, checkin_ = -1;
  int vis_[8] = {}, nVis_ = 0;   // sichtbare Apps (nicht abgeschaltet)
  uint32_t seed_ = 0x9e3779b9;
  float clock_ = 0, askT_ = 4, saveT_ = 30;
  uint32_t lastSeen_ = 0;   // ms, zuletzt auf dem Dashboard (Begrüßung nur nach längerer Pause)
  int64_t epoch_ = 0;

  Pet pet_;
  float R_ = 46;            // aktueller Radius (gleitet zur Stufengröße)
  float hatchT_ = -1, growT_ = -1;
  // Grummels Zustand
  float purr_ = 0, giggle_ = 0, dizzy_ = 0, listen_ = 0, startle_ = 0, blink_ = 3, lookX_ = 0, lookY_ = 0;
  float sq_ = 0, sqv_ = 0, strokeAcc_ = 0, cuddleAcc_ = 0, voiceT_ = 0, chew_ = 0;
  float foodT_ = -1;        // Apfel fliegt (0..1), dann wird gekaut
  bool onBuddy_ = false, hadFinger_ = false;
  float lastFX_ = 0, lastFY_ = 0;
  // Sprechblase
  char bubble_[48] = "";
  float bubbleT_ = 0, bubbleDur_ = 1;
  bool bubbleAsk_ = false;
  // Herzchen beim Streicheln
  struct Heart { float x, y, t; };
  Heart hearts_[12];
  int nHearts_ = 0;
};

}  // namespace dk
