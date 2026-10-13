// Dashboard: Startseite mit Grummel als Freund und den App-Symbolen.
// Grummel reagiert aufs Streicheln (Finger über ihn ziehen), Antippen, Schütteln und auf Stimme (nur der Pegel).
// Ab und zu fragt er in einer Sprechblase „Wie geht's dir?“; Antippen der Blase öffnet den Check-in.
#pragma once
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Home : public Tile {
 public:
  struct AppIcon { const char* label; int tile; int kind; };   // kind: 0 Gefühle, 1 Grummel, 2 Atmen, 3 Kugel, 4 Pferde
  Home(Synth& synth, Store& store);
  void setApps(const AppIcon* apps, int n) { apps_ = apps; nApps_ = n; }
  void setCheckIn(int tile) { checkin_ = tile; }
  const char* title() const override { return "Start"; }
  void enter(uint32_t ms) override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool wantsMotion() const override { return true; }
  bool claimsDrag() const override { return onBuddy_; }   // Streicheln öffnet keine Einstellungen
  bool showsClose() const override { return false; }

  static void iconPos(int i, int n, float& x, float& y);
  static constexpr float BUDDY_X = W / 2, BUDDY_Y = 134, BUDDY_R = 50;   // Grummel

 private:
  void say(const char* text, float secs);
  void drawBuddy(Scene& s);
  void drawIcon(Scene& s, const AppIcon& a, float x, float y);
  int cardsWaiting() const;
  float rnd();

  Synth& synth_;
  Store& store_;
  const AppIcon* apps_ = nullptr;
  int nApps_ = 0, checkin_ = -1;
  uint32_t seed_ = 0x9e3779b9;
  float clock_ = 0, idle_ = 0;
  uint32_t lastSeen_ = 0;   // ms, zuletzt auf dem Dashboard (Begrüßung nur nach längerer Pause)
  // Grummels Zustand
  float purr_ = 0, giggle_ = 0, dizzy_ = 0, listen_ = 0, startle_ = 0, blink_ = 3, lookX_ = 0, lookY_ = 0;
  float sq_ = 0, sqv_ = 0, strokeAcc_ = 0, voiceT_ = 0;
  bool onBuddy_ = false;
  float lastFX_ = 0, lastFY_ = 0;
  bool hadFinger_ = false;
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
