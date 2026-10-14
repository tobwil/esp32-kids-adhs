#include "home.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "checkin.h"
#include "fonts.h"
#include "horses.h"
#include "icons.h"
#include "rewards.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), INK = hex(0x1c1228), EYE = hex(0xfbf8ff);
static const Col BODY_TOP = hex(0x86e3c9), BODY_BOT = hex(0x3aa98f), CHEEK = hex(0xff9ec0);
static const Col ICON_BG = hex(0x1d2540), ICON_RIM = hex(0x2e3a62), GOLD = hex(0xffd166);
static const Col NEED_COL[Pet::N_NEEDS] = {hex(0xffa64d), hex(0xff8fb8), hex(0x5fd8c8)};
static constexpr float NEED_R = 15;
static constexpr float BUBBLE_Y = 58, BUBBLE_H = 40;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float approach(float v, float t, float rate, float dt) { return v + (t - v) * std::fmin(1.f, rate * dt); }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

Home::Home(Synth& synth, Store& store) : synth_(synth), store_(store) {
  pet_.load(store_);
  R_ = pet_.size();
}

float Home::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return (seed_ & 0xFFFFFF) / float(0x1000000);
}

static inline int wrapi(int i, int n) { return ((i % n) + n) % n; }

bool Home::focusApp(int tile) {
  for (int i = 0; i < nVis_; i++)
    if (apps_[vis_[i]].tile == tile) { carIdx_ = i; carPos_ = float(i); carDrag_ = 0; return true; }
  return false;
}

int Home::focusedTile() const { return nVis_ ? apps_[vis_[wrapi(carIdx_, nVis_)]].tile : -1; }

void Home::carouselTick(const TileInput& in) {
  if (!nVis_) return;
  // Wischen im Karussell: das Bild folgt dem Finger, beim Loslassen rastet es auf die nächste App ein
  if (in.finger && !carFinger_ && in.fingerY > CAR_TOP) { carDragging_ = true; carStartX_ = in.fingerX; }
  if (carDragging_ && in.finger) carDrag_ = in.fingerX - carStartX_;
  if (carDragging_ && !in.finger) {
    const float p = carPos_ - carDrag_ / CAR_DX;   // sichtbare Stellung beim Loslassen
    int target = int(std::floor(p + 0.5f));
    if (std::fabs(carDrag_) > 40 && target == carIdx_) target += carDrag_ < 0 ? 1 : -1;   // kurzer Wisch reicht für eine App
    carPos_ = p;
    carDrag_ = 0;
    carIdx_ = target;
    carDragging_ = false;
    store_.set("car_i", wrapi(carIdx_, nVis_));
  }
  carFinger_ = in.finger;
  carPos_ = approach(carPos_, float(carIdx_), 10, in.dt);
}

bool Home::carouselTap(float x, float y) {
  if (!nVis_ || y < CAR_TOP) return false;
  if (std::fabs(x - W / 2) < 82) { requestGoto(focusedTile()); return true; }   // Mitte: öffnen
  carIdx_ += x < W / 2 ? -1 : 1;   // Nachbar in die Mitte holen
  store_.set("car_i", wrapi(carIdx_, nVis_));
  return true;
}

bool Home::claimsDragAt(float x, float y) const {
  // Streicheln beginnt auf Grummel (oder dem Ei), im Karussell dreht Wischen die Apps: dort blättert Wischen nicht
  if (y > CAR_TOP) return true;
  const float dx = x - BUDDY_X, dy = y - buddyY(), r = R_ * 1.5f;
  return dx * dx + dy * dy < r * r;
}

void Home::needPos(int i, float& x, float& y) {
  x = 132 + i * 48;
  y = 30;
}

int Home::cardsWaiting() const { return album_ ? album_->pending() : 0; }

bool Home::asleep() const {
  if (epoch_ <= 0 || pet_.stage == Pet::EGG) return false;
  const int h = int((epoch_ / 3600) % 24);
  return h >= 20 || h < 7;   // Schlafenszeit 20 bis 7 Uhr (nur wenn die Uhrzeit bekannt ist)
}

void Home::say(const char* text, float secs, bool ask) {
  snprintf(bubble_, sizeof bubble_, "%s", text);
  bubbleT_ = bubbleDur_ = secs;
  bubbleAsk_ = ask;
  bubbleTo_ = ask ? checkin_ : -1;
}

void Home::feed() {
  if (pet_.need[Pet::FOOD] > 0.9f) { say("Ich bin satt, danke!", 2); return; }
  if (foodT_ >= 0 || chew_ > 0) return;
  foodT_ = 0;
}

void Home::enter(uint32_t ms) {
  if (pet_.applyEvents(store_)) pet_.save(store_);
  if (pet_.stage == Pet::EGG) { bubbleT_ = 0; }
  else if (!lastSeen_ || ms - lastSeen_ > 30000) { say(asleep() ? "Gute Nacht!" : "Hallo!", 2.2f); askT_ = 4; }
  else { bubbleT_ = 0; askT_ = 6; }
  sqv_ = 2.5f;   // kleiner Hüpfer
}

void Home::tick(const TileInput& in) {
  const float dt = in.dt;
  clock_ += dt;
  nVis_ = 0;   // abgeschaltete Apps verschwinden vom Dashboard
  for (int i = 0; i < nApps_ && nVis_ < 8; i++) if (!(in.disabled >> apps_[i].tile & 1)) vis_[nVis_++] = i;
  if (!started_) { started_ = true; carIdx_ = store_.get("car_i", 0); carPos_ = float(carIdx_); }
  carouselTick(in);
  lastSeen_ = in.ms;
  epoch_ = in.epoch;
  bubbleT_ = std::fmax(0, bubbleT_ - dt);
  giggle_ = std::fmax(0, giggle_ - dt);
  dizzy_ = std::fmax(0, dizzy_ - dt);
  startle_ = std::fmax(0, startle_ - dt * 2);
  chew_ = std::fmax(0, chew_ - dt);
  if (hatchT_ >= 0) hatchT_ += dt;
  if (growT_ >= 0 && (growT_ += dt) > 3) growT_ = -1;
  const bool sleeping = asleep();
  if (!sleeping) pet_.update(dt);
  R_ = approach(R_, pet_.size() * (growT_ >= 0 ? 1.08f : 1.f), 3, dt);
  if ((saveT_ -= dt) < 0) { saveT_ = 30; pet_.save(store_); }

  // Neue Post: Grummel sagt Bescheid (Tipp auf die Blase öffnet die Post), alle 30 s wieder, solange ungehört
  postAskT_ -= dt;
  Message newest;
  if (msgs_ && postTile_ >= 0 && !(in.disabled >> postTile_ & 1) && pet_.stage != Pet::EGG && bubbleT_ <= 0 && postAskT_ <= 0 && msgs_->newest(newest)) {
    char b[48];
    snprintf(b, sizeof b, "Post von %.20s!", newest.from);
    say(b, 8, true);
    bubbleTo_ = postTile_;
    postAskT_ = 30;
    sqv_ += 2;
  }

  // Ab und zu: ein Wunsch oder „Wie geht's dir?“ (nie vorwurfsvoll)
  if (pet_.stage != Pet::EGG && !sleeping && bubbleT_ <= 0 && (askT_ -= dt) < 0) {
    int low = -1;
    for (int i = 0; i < Pet::N_NEEDS; i++) if (pet_.need[i] < 0.35f && (low < 0 || pet_.need[i] < pet_.need[low])) low = i;
    static const char* const WISH[3] = {"Ich hab ein bisschen Hunger.", "Kuschelst du mit mir?", "Spielen wir was?"};
    if (low >= 0 && rnd() < 0.6f) say(WISH[low], 5);
    else if (checkin_ >= 0) say("Wie geht's dir?", 8, true);
    askT_ = 40;
  }

  // ---- Ei: warm streicheln oder antippen, dann schlüpft Grummel ----
  if (pet_.stage == Pet::EGG) {
    if (in.tap) { pet_.warmth += 0.08f; sqv_ += 2; }
    if (in.finger) {
      const float dx = in.fingerX - BUDDY_X, dy = in.fingerY - buddyY();
      onBuddy_ = dx * dx + dy * dy < (R_ * 1.6f) * (R_ * 1.6f);
      if (onBuddy_ && hadFinger_) {
        const float d = std::hypot(in.fingerX - lastFX_, in.fingerY - lastFY_);
        pet_.warmth += d / 1500;
        sqv_ += d * 0.02f;
      }
      lastFX_ = in.fingerX; lastFY_ = in.fingerY;
    } else onBuddy_ = false;
    hadFinger_ = in.finger;
    if (in.shaking) pet_.warmth += dt * 0.15f;
    if (pet_.warmth >= 1 && hatchT_ < 0) {
      hatchT_ = 0;
      for (int k = 0; k < 5; k++) { Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = 523.f * std::pow(1.19f, float(k)); v.delay = 0.08f * k; v.dur = 0.3f; v.vol = 0.13f; synth_.play(v); }
    }
    if (hatchT_ > 0.6f) {   // geschlüpft
      pet_.stage = Pet::BABY;
      pet_.save(store_);
      R_ = pet_.size();
      hatchT_ = -1;
      say("Hallo! Ich bin Grummel!", 3.5f);
      askT_ = 8;
      sqv_ = 4;
    }
    sqv_ += (-80.f * sq_ - 6.f * sqv_) * dt;
    sq_ = clampf(sq_ + sqv_ * dt, -0.14f, 0.14f);
    if (pet_.stage == Pet::EGG) {
      // Apps sind auch vor dem Schlüpfen erreichbar
      if (in.tap && carouselTap(in.tapX, in.tapY)) pet_.warmth -= 0.08f;
      return;
    }
  }

  // ---- Antippen ----
  if (in.tap && carouselTap(in.tapX, in.tapY)) askT_ = std::fmax(askT_, 10);
  else if (in.tap) {
    askT_ = std::fmax(askT_, 10);
    const float bw = Scene::textW(fonts::body, bubble_) + 36;
    for (int i = 0; i < Pet::N_NEEDS; i++) {   // Bedürfnis-Symbole oben
      float x, y;
      needPos(i, x, y);
      if ((in.tapX - x) * (in.tapX - x) + (in.tapY - y) * (in.tapY - y) < (NEED_R + 10) * (NEED_R + 10)) {
        if (i == Pet::FOOD) feed();
        else if (i == Pet::CUDDLE) say("Streichel mich!", 2);
        else for (int k = 0; k < nVis_; k++) if (apps_[vis_[k]].kind == 3) requestGoto(apps_[vis_[k]].tile);
        return;
      }
    }
    if (bubbleT_ > 0 && bubbleAsk_ && in.tapY > BUBBLE_Y - 6 && in.tapY < BUBBLE_Y + BUBBLE_H + 8 && std::fabs(in.tapX - W / 2) < bw / 2 + 8) {
      requestGoto(bubbleTo_ >= 0 ? bubbleTo_ : checkin_);
      bubbleT_ = 0;
      return;
    }
    const float dx = in.tapX - BUDDY_X, dy = in.tapY - buddyY();
    if (dx * dx + dy * dy < (R_ + 14) * (R_ + 14)) {
      if (sleeping) { say("Psst … ich schlafe.", 2); return; }
      giggle_ = 1.1f;
      sqv_ += 3;
      say(rnd() < 0.5f ? "Hihi!" : "Das kitzelt!", 1.6f);
      for (int k = 0; k < 3; k++) { Synth::Voice v; v.wave = Synth::TRI; v.f0 = v.f1 = 520.f + 130 * k; v.delay = 0.08f * k; v.dur = 0.1f; v.vol = 0.12f; synth_.play(v); }
    }
  }

  // ---- Füttern: Apfel fliegt in den Mund, dann wird gekaut ----
  if (foodT_ >= 0) {
    foodT_ += dt / 0.7f;
    if (foodT_ >= 1) {
      foodT_ = -1;
      chew_ = 1.2f;
      pet_.need[Pet::FOOD] = std::fmin(1.f, pet_.need[Pet::FOOD] + 0.35f);
      if (pet_.addGrowth(1)) { growT_ = 0; say("Ich bin gewachsen!", 3); }
      else if (pet_.need[Pet::FOOD] > 0.9f) say("Mmh, jetzt bin ich satt!", 2.2f);
      else say("Mmh, lecker!", 1.6f);
      pet_.save(store_);
      Synth::Voice v; v.wave = Synth::NOISE; v.filt = Synth::LP; v.cut0 = 500; v.cut1 = 250; v.dur = 0.09f; v.vol = 0.3f; synth_.play(v);
    }
  }

  // ---- Streicheln: Finger zieht über Grummel ----
  onBuddy_ = false;
  if (in.finger) {
    const float dx = in.fingerX - BUDDY_X, dy = in.fingerY - buddyY();
    onBuddy_ = dx * dx + dy * dy < (R_ * 1.5f) * (R_ * 1.5f);
    if (onBuddy_ && hadFinger_) {
      const float d = std::hypot(in.fingerX - lastFX_, in.fingerY - lastFY_);
      if (d > 1.2f) {
        strokeAcc_ += d;
        cuddleAcc_ += d;
        purr_ = approach(purr_, 1, 3, dt);
        sqv_ += d * 0.02f;
        pet_.need[Pet::CUDDLE] = std::fmin(1.f, pet_.need[Pet::CUDDLE] + d / 3000);
        if (cuddleAcc_ > 900) { cuddleAcc_ = 0; if (pet_.addGrowth(1)) { growT_ = 0; say("Ich bin gewachsen!", 3); } }
        if (strokeAcc_ > 70 && nHearts_ < 12) {   // Herzchen steigen auf
          strokeAcc_ = 0;
          hearts_[nHearts_++] = {BUDDY_X + (rnd() - 0.5f) * R_ * 1.4f, buddyY() - R_ * 0.6f, 0};
          Synth::Voice v; v.wave = Synth::NOISE; v.filt = Synth::LP; v.cut0 = v.cut1 = 220; v.amF = 24; v.amD = 0.8f; v.attack = 0.05f; v.dur = 0.5f; v.vol = 0.2f; synth_.play(v);
          if (bubbleT_ <= 0 && rnd() < 0.35f) say(rnd() < 0.5f ? "Mmmh, schön!" : "Das mag ich!", 1.8f);
        }
      }
    }
    lastFX_ = in.fingerX; lastFY_ = in.fingerY;
    askT_ = std::fmax(askT_, 10);
  }
  hadFinger_ = in.finger;
  if (!onBuddy_) purr_ = approach(purr_, 0, 1.2f, dt);

  // ---- Schütteln, Klopfen ----
  if (in.shaking) {
    if (dizzy_ < 0.5f) { say("Huiii!", 1.5f); Synth::Voice v; v.wave = Synth::SINE; v.f0 = 900; v.f1 = 300; v.dur = 0.5f; v.vol = 0.15f; synth_.play(v); }
    dizzy_ = 1.6f;
    sqv_ += (rnd() - 0.5f) * 6 * in.shake;
  }
  if (in.nKnocks > 0 && dizzy_ <= 0) { startle_ = 1; sqv_ -= 2; if (bubbleT_ <= 0) say("Huch!", 1.2f); }

  // ---- Stimme: Grummel hört zu ----
  if (in.voice > 0.15f) { voiceT_ += dt; listen_ = approach(listen_, 1, 6, dt); askT_ = std::fmax(askT_, 10); }
  else {
    if (voiceT_ > 0.8f && bubbleT_ <= 0) say(rnd() < 0.5f ? "Ich hör dir zu!" : "Erzähl mir mehr!", 2);
    voiceT_ = 0;
    listen_ = approach(listen_, 0, 2, dt);
  }

  // Blick, Blinzeln, Feder, Herzchen
  float tx = std::sin(clock_ * 0.37f) * 0.5f, ty = std::sin(clock_ * 0.23f) * 0.3f;
  if (in.finger) { tx = clampf((in.fingerX - BUDDY_X) / 120, -1, 1); ty = clampf((in.fingerY - buddyY()) / 120, -1, 1); }
  if (foodT_ >= 0) { tx = -0.6f; ty = -0.8f; }   // schaut dem Apfel entgegen
  lookX_ = approach(lookX_, tx, 6, dt);
  lookY_ = approach(lookY_, ty, 6, dt);
  blink_ -= dt;
  if (blink_ < -0.13f) blink_ = 2.5f + rnd() * 3.5f;
  sqv_ += (-80.f * sq_ - 6.f * sqv_) * dt;
  sq_ = clampf(sq_ + sqv_ * dt, -0.14f, 0.14f);
  for (int i = 0; i < nHearts_;) {
    hearts_[i].t += dt;
    if (hearts_[i].t > 1.4f) { hearts_[i] = hearts_[--nHearts_]; continue; }
    i++;
  }
}

// ---------------- Zeichnen ----------------

void Home::drawEgg(Scene& s) {
  const float w = pet_.warmth, k = R_ / 44;   // k: gezeichnet für Radius 44
  const float shake = (w > 0.5f ? std::sin(clock_ * 20) * 2 * (w - 0.5f) : 0) + sq_ * 30;
  const float x = BUDDY_X + shake, y = buddyY();
  if (hatchT_ >= 0) {   // Schale springt auf, Licht
    const float u = smooth(hatchT_ / 0.6f);
    s.glow(x, y, 150 * u + 1, hex(0xfff0b0), 0.6f * (1 - u * 0.5f));
    s.ellipse(x - 40 * u, y - 14 - 50 * u, 34 * k, 26 * k, hex(0xfff3d6), hex(0xf0c890), 1 - u);
    s.ellipse(x + 40 * u, y + 26 * u, 34 * k, 26 * k, hex(0xfff3d6), hex(0xf0c890), 1 - u);
    return;
  }
  s.glow(x, y, 80 * k, hex(0xffd88a), 0.12f + 0.25f * w);
  s.ellipse(x, y, 36 * k, 46 * k, hex(0xfff3d6), hex(0xf0c890));
  s.circle(x - 14 * k, y - 12 * k, 6 * k, hex(0x86e3c9), 0.7f);
  s.circle(x + 12 * k, y + 8 * k, 8 * k, hex(0x86e3c9), 0.7f);
  s.circle(x - 6 * k, y + 24 * k, 5 * k, hex(0x86e3c9), 0.7f);
  s.ellipse(x - 12 * k, y - 24 * k, 8 * k, 5 * k, hex(0xffffff), 0.5f);
  const Col crack = hex(0x8a6a40);
  if (w > 0.35f) {   // Risse
    s.capsule(x - 20 * k, y - 6 * k, x - 10 * k, y + 2 * k, 1.6f, crack);
    s.capsule(x - 10 * k, y + 2 * k, x - 2 * k, y - 8 * k, 1.6f, crack);
  }
  if (w > 0.7f) {
    s.capsule(x - 2 * k, y - 8 * k, x + 8 * k, y + 1 * k, 1.6f, crack);
    s.capsule(x + 8 * k, y + 1 * k, x + 18 * k, y - 7 * k, 1.6f, crack);
  }
  const float a = 0.6f + 0.3f * std::sin(clock_ * 2.5f);
  s.text(fonts::body, "Streichel das Ei!", W / 2, 64, TXT, a, Align::Center);
}

void Home::drawBuddy(Scene& s) {
  const float R = R_;
  const bool baby = pet_.stage == Pet::BABY, sleeping = asleep();
  const float breathe = 1 + (sleeping ? 0.04f : 0.02f) * std::sin(clock_ * (sleeping ? 0.9f : 1.6f));
  const float x = BUDDY_X + listen_ * 2, y = buddyY() - 4 * smooth(giggle_) - startle_ * 6;
  const float rx = R * (1 + sq_) * breathe, ry = R * (1 - sq_) * breathe;
  if (growT_ >= 0)   // Wachsen: Funkeln
    for (int k = 0; k < 8; k++) {
      const float an = k * TAU / 8 + growT_ * 2, rr = R * 1.4f + 20 * growT_;
      s.star(x + rr * std::sin(an), y - rr * std::cos(an), 7, GOLD, 1 - smooth((growT_ - 2) / 1), an);
    }
  s.glow(x, y, R * 1.5f, hex(0x3fc0a0), 0.22f, R * 0.85f);
  s.ellipse(x - rx * 0.38f, y + ry * 0.92f, R * 0.2f, R * 0.1f, scale(BODY_BOT, 0.8f));
  s.ellipse(x + rx * 0.38f, y + ry * 0.92f, R * 0.2f, R * 0.1f, scale(BODY_BOT, 0.8f));
  const float wa = 0.01f + 0.03f * (dizzy_ > 0 ? 1.f : 0.f) + 0.012f * purr_;
  const float wob[6] = {wa, clock_ * 2.1f, wa * 0.5f, -clock_ * 2.9f + 1, wa * 0.3f, clock_ * 3.7f + 2};
  s.blob(x, y, rx, ry, wob, BODY_TOP, BODY_BOT);
  if (baby) s.arc(x + 4, y - ry - 6, 7, 3.5f, -2.2f, 1.6f, BODY_BOT);   // Haarlocke
  s.ellipse(x - rx * 0.36f, y - ry * 0.52f, rx * 0.24f, ry * 0.12f, hex(0xffffff), 0.18f);
  const float ch = 1 + 0.5f * purr_;
  s.ellipse(x - rx * 0.56f, y + ry * 0.14f, R * 0.13f * ch, R * 0.08f * ch, CHEEK, 0.4f + 0.3f * purr_);
  s.ellipse(x + rx * 0.56f, y + ry * 0.14f, R * 0.13f * ch, R * 0.08f * ch, CHEEK, 0.4f + 0.3f * purr_);
  // Schmuck für gesammelte Sterne
  const int acc = Pet::accessory(starsGet(store_));
  const float hx = x + rx * 0.45f, hy = y - ry * 0.78f, m = R / 52;   // m: gezeichnet für Radius 52
  if (acc == 1) {   // Schleife
    s.ellipse(hx - 9 * m, hy, 9 * m, 6 * m, hex(0xff6fa8)); s.ellipse(hx + 9 * m, hy, 9 * m, 6 * m, hex(0xff6fa8)); s.circle(hx, hy, 4.5f * m, hex(0xffa6c9));
  } else if (acc == 2) {   // Blume
    for (int k = 0; k < 5; k++) s.circle(hx + 8 * m * std::sin(k * TAU / 5), hy - 8 * m * std::cos(k * TAU / 5), 5.5f * m, hex(0xffe08a));
    s.circle(hx, hy, 4.5f * m, hex(0xff9a5a));
  } else if (acc == 3) {   // Krone
    const float cx = x, cy = y - ry - 4 * m;
    s.rrect(cx - 18 * m, cy - 4 * m, 36 * m, 10 * m, 3 * m, GOLD);
    for (int k = -1; k <= 1; k++) s.star(cx + k * 12 * m, cy - 8 * m, 6 * m, GOLD);
    s.circle(cx, cy + 1 * m, 2.5f * m, hex(0xff6b5a));
  }
  // Augen
  const float e = R * (baby ? 0.23f : 0.19f);
  for (int sd = -1; sd <= 1; sd += 2) {
    const float ex = x + sd * rx * 0.33f, ey = y - ry * 0.12f;
    if (dizzy_ > 0) {   // Kringelaugen
      const float rot = clock_ * 8 * sd;
      s.arc(ex, ey, e * 0.8f, 3, rot, rot + 4.5f, INK);
      s.arc(ex, ey, e * 0.38f, 3, rot + 2, rot + 6, INK);
    } else if (giggle_ > 0 || chew_ > 0) {
      s.arc(ex, ey + e * 0.35f, e * 0.75f, 4.5f, -1.15f, 1.15f, INK);
    } else if (purr_ > 0.4f || sleeping) {
      s.arc(ex, ey - e * 0.25f, e * 0.75f, 4, PIF - 1.15f, PIF + 1.15f, INK);
    } else if (blink_ < 0) {
      s.capsule(ex - e * 0.75f, ey, ex + e * 0.75f, ey, 2.2f, INK);
    } else {
      const float big = 1 + 0.15f * listen_ + 0.2f * startle_;
      s.ellipse(ex, ey, e * big, e * 1.15f * big, EYE);
      const float px = ex + lookX_ * e * 0.35f, py = ey + lookY_ * e * 0.35f - listen_ * e * 0.2f;
      s.circle(px, py, e * 0.55f, INK);
      s.circle(px - e * 0.18f, py - e * 0.2f, e * 0.16f, EYE, 0.9f);
    }
  }
  // Mund
  const float my = y + ry * 0.32f;
  if (chew_ > 0) {
    const float o = std::fabs(std::sin(chew_ * 12));
    s.ellipse(x, my, R * 0.12f, R * (0.03f + 0.09f * o), INK);
  } else if (giggle_ > 0) {
    s.ellipse(x, my, R * 0.16f, R * 0.13f, INK);
    s.ellipse(x, my + R * 0.05f, R * 0.09f, R * 0.05f, hex(0xe0607e));
  } else if (dizzy_ > 0) {
    for (int k = 0; k < 4; k++) s.capsule(x - R * 0.2f + k * R * 0.1f, my + (k & 1 ? 3 : -3), x - R * 0.1f + k * R * 0.1f, my + (k & 1 ? -3 : 3), 1.8f, INK);
  } else if (listen_ > 0.4f) {
    s.ellipse(x, my, R * 0.06f, R * 0.08f, INK);
    for (int k = 1; k <= 2; k++)   // Schallwellen am Ohr
      s.arc(x + rx + 4, y - ry * 0.1f, 8.f * k, 2.5f, 0.6f, 2.5f, hex(0x8fe3d0), (0.7f - 0.2f * k) * listen_);
  } else {
    s.arc(x, my - R * 0.1f, R * 0.13f, 3.5f, PIF - 0.95f, PIF + 0.95f, INK);
  }
  if (sleeping) {   // z z z
    for (int k = 0; k < 2; k++) {
      const float u = std::fmod(clock_ * 0.35f + k * 0.5f, 1.f);
      s.text(fonts::body, "z", x + rx * 0.8f + u * 14, y - ry - u * 26, hex(0xd8d0ff), (1 - u) * 0.8f, Align::Center);
    }
  }
  for (int i = 0; i < nHearts_; i++) {
    const Heart& h = hearts_[i];
    heart(s, h.x + 6 * std::sin(h.t * 5 + i), h.y - 50 * h.t, 9, hex(0xff8fb8), 1 - smooth((h.t - 0.8f) / 0.6f));
  }
  if (foodT_ >= 0) {   // Apfel fliegt im Bogen zum Mund
    float sx, sy;
    needPos(Pet::FOOD, sx, sy);
    const float u = smooth(foodT_), tx = x, ty = my;
    apple(s, sx + (tx - sx) * u, sy + (ty - sy) * u - 50 * std::sin(u * PIF), 12 - 4 * u, 1);
  }
}

void Home::drawNeeds(Scene& s) {
  for (int i = 0; i < Pet::N_NEEDS; i++) {
    float x, y;
    needPos(i, x, y);
    const float v = pet_.need[i];
    s.circle(x, y, NEED_R + 1, ICON_BG);
    s.ring(x, y, NEED_R + 4, 3, hex(0x2a3050));
    s.arc(x, y, NEED_R + 4, 3, 0, std::fmax(0.15f, v * TAU), NEED_COL[i], 0.9f);
    if (i == Pet::FOOD) apple(s, x, y + 1, 11, 1);
    else if (i == Pet::CUDDLE) heart(s, x, y - 1, 8.5f, NEED_COL[i], 1);
    else ball(s, x, y, 9, 1);
  }
}

void Home::drawIcon(Scene& s, const AppIcon& a, float x, float y, float ICON_R, bool label) {
  const float K = ICON_R / 30;   // Symbole sind für Radius 30 gezeichnet
  s.circle(x, y, ICON_R + 3, ICON_RIM);
  s.circle(x, y, ICON_R, ICON_BG);
  switch (a.kind) {
    case 0: drawFace(s, CheckIn::FROH, x, y, 22 * K, clock_); break;
    case 1: {   // kleiner Grummel mit Wutfunken
      s.ellipse(x, y + 3 * K, 19 * K, 17 * K, BODY_TOP, BODY_BOT);
      s.circle(x - 6 * K, y, 3.5f * K, EYE); s.circle(x + 6 * K, y, 3.5f * K, EYE);
      s.circle(x - 6 * K, y, 1.8f * K, INK); s.circle(x + 6 * K, y, 1.8f * K, INK);
      for (int k = 0; k < 3; k++) {
        const float an = clock_ * 1.5f + k * 2.1f;
        s.glow(x + 22 * K * std::sin(an), y - 19 * K * std::cos(an), 7 * K, hex(0xff5a2c), 0.7f);
      }
      break;
    }
    case 2:   // Ballon
      s.capsule(x, y + 15 * K, x + 2 * K, y + 26 * K, 1, hex(0xd8d0e8));
      s.ellipse(x, y - 2 * K, 15 * K, 18 * K, hex(0xffb8cc), hex(0xe8668e));
      s.ellipse(x - 5 * K, y - 9 * K, 4 * K, 3 * K, hex(0xffffff), 0.4f);
      break;
    case 3:   // Kugel und Loch
      s.circle(x + 7 * K, y + 6 * K, 11 * K, hex(0x02040a));
      s.ring(x + 7 * K, y + 6 * K, 12 * K, 2, hex(0x3a4a70));
      s.ellipse(x - 9 * K, y - 7 * K, 9 * K, 9 * K, hex(0xffdf80), hex(0xe8a020));
      break;
    case 5:   // Rätsel: Fragezeichen in einer Sprechblase
      s.circle(x, y - 2 * K, 19 * K, hex(0xb7a6ff));
      s.capsule(x - 10 * K, y + 12 * K, x - 15 * K, y + 20 * K, 3 * K, hex(0xb7a6ff));
      s.text(fonts::title, "?", x, y - 2 - fonts::title.lineH / 2.f + 2, hex(0x241c48), 1, Align::Center);
      break;
    case 6:   // Radio
      s.capsule(x - 4 * K, y - 10 * K, x + 12 * K, y - 22 * K, 1.4f * K, hex(0xd8d0e8));
      s.rrect(x - 17 * K, y - 10 * K, 34 * K, 24 * K, 6 * K, hex(0x8f7fe0));
      s.circle(x - 6 * K, y + 2 * K, 7 * K, hex(0x2a1f5a));
      s.circle(x - 6 * K, y + 2 * K, 3 * K, hex(0xb7a6ff));
      s.capsule(x + 6 * K, y - 3 * K, x + 11 * K, y - 3 * K, 1.6f * K, hex(0xfff0c8));
      s.capsule(x + 6 * K, y + 3 * K, x + 11 * K, y + 3 * K, 1.6f * K, hex(0xfff0c8));
      s.capsule(x + 6 * K, y + 8 * K, x + 11 * K, y + 8 * K, 1.6f * K, hex(0xfff0c8));
      break;
    case 7: {   // Post: Briefumschlag, Abzeichen bei ungehörter Nachricht
      s.rrect(x - 17 * K, y - 11 * K, 34 * K, 23 * K, 4 * K, hex(0xfff3d6));
      s.capsule(x - 15 * K, y - 9 * K, x, y + 2 * K, 1.6f * K, hex(0xc8a070));
      s.capsule(x + 15 * K, y - 9 * K, x, y + 2 * K, 1.6f * K, hex(0xc8a070));
      if (msgs_ && msgs_->unheard() > 0) {
        const float p = 0.8f + 0.2f * std::sin(clock_ * 4);
        s.circle(x + ICON_R * 0.75f, y - ICON_R * 0.75f, 13 * p * K, hex(0xff7a9a));
        s.text(fonts::small, "!", x + ICON_R * 0.75f, y - ICON_R * 0.75f - 11, hex(0x2a1a00), 1, Align::Center);
      }
      break;
    }
    default: {   // Sammelalbum: Pferd oder Kartenstapel mit Stern
      static const Horse H = {"", 150, Coat::CHESTNUT, 0xc07a3a, 0xf0dca0, false};
      if (!album_ || album_->horses()) drawHorse(s, H, x - 2 * K, y + 20 * K, 0.36f * K, clock_ * 0.3f);
      else {
        s.rrect(x - 13 * K, y - 15 * K, 24 * K, 32 * K, 4 * K, hex(0x3a2a80));
        s.rrect(x - 9 * K, y - 17 * K, 24 * K, 32 * K, 4 * K, hex(0x6a4cff));
        s.star(x + 3 * K, y - 1 * K, 8 * K, GOLD);
      }
      if (cardsWaiting() > 0) {   // neue Karte wartet
        const float p = 0.8f + 0.2f * std::sin(clock_ * 4);
        s.circle(x + ICON_R * 0.75f, y - ICON_R * 0.75f, 13 * p * K, GOLD);
        s.text(fonts::small, "!", x + ICON_R * 0.75f, y - ICON_R * 0.75f - 11, hex(0x2a1a00), 1, Align::Center);
      }
      break;
    }
  }
  if (label) s.text(fonts::body, a.kind == 4 && album_ ? album_->shortName() : a.label, x, y + ICON_R + 6, TXT, 1, Align::Center);
}

void Home::draw(Scene& s) {
  if (pet_.stage == Pet::EGG) drawEgg(s);
  else {
    drawNeeds(s);
    drawBuddy(s);
  }
  drawStarCount(s, 300, 18, starsGet(store_), 0.85f);
  // Sprechblase
  if (bubbleT_ > 0 && bubble_[0]) {
    const float a = std::fmin(1.f, bubbleT_ / 0.3f) * std::fmin(1.f, (bubbleDur_ - bubbleT_) / 0.25f);
    const float w = std::fmin(330.f, Scene::textW(fonts::body, bubble_) + 36.f);
    const Col bg = bubbleAsk_ ? hex(0xffe9a8) : hex(0xf2eefa);
    s.rrect(W / 2 - w / 2, BUBBLE_Y, w, BUBBLE_H, BUBBLE_H / 2, bg, a);
    s.circle(W / 2 + 14, BUBBLE_Y + BUBBLE_H + 5, 5, bg, a);
    s.text(fonts::body, bubble_, W / 2, BUBBLE_Y + (BUBBLE_H - fonts::body.lineH) / 2 + 1, hex(0x2a1a30), a, Align::Center);
  }
  drawCarousel(s);
}

void Home::drawCarousel(Scene& s) {
  if (!nVis_) return;
  const float p = carPos_ - carDrag_ / CAR_DX;
  const int c = int(std::floor(p + 0.5f));
  // nur so viele Nachbarn, dass keine App doppelt erscheint; außen zuerst, die Mitte zuletzt
  const int kl = nVis_ >= 3 ? 1 : 0, kr = nVis_ >= 2 ? 1 : 0;
  int order[5], n = 0;
  for (int k = -kl - (nVis_ >= 5 ? 1 : 0); k <= kr + (nVis_ >= 5 ? 1 : 0); k++) order[n++] = k;
  for (int i = 0; i < n; i++)   // nach Abstand sortieren
    for (int j = i + 1; j < n; j++)
      if (std::fabs(c + order[j] - p) > std::fabs(c + order[i] - p)) { const int t = order[i]; order[i] = order[j]; order[j] = t; }
  const float fade0 = s.fade;
  for (int i = 0; i < n; i++) {
    const float rel = c + order[i] - p, f = std::fmin(1.f, std::fabs(rel));
    if (std::fabs(rel) > 2.2f) continue;
    const float x = W / 2 + rel * CAR_DX, r = CAR_R * (1 - 0.4f * f);
    s.fade = fade0 * (1 - 0.55f * f) * (1 - smooth((std::fabs(rel) - 1.4f) / 0.6f));
    drawIcon(s, apps_[vis_[wrapi(c + order[i], nVis_)]], x, CAR_Y + 10 * f, r, f < 0.35f);
  }
  s.fade = fade0;
  // Punkte: wo im Karussell man gerade ist
  const int cur = wrapi(c, nVis_);
  for (int i = 0; i < nVis_; i++) s.circle(W / 2 + (i - (nVis_ - 1) * 0.5f) * 13, H - 18, i == cur ? 4.f : 2.6f, i == cur ? TXT : hex(0x4a5070));
}

}  // namespace dk
