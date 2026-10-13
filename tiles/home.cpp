#include "home.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "checkin.h"
#include "fonts.h"
#include "horses.h"
#include "rewards.h"

namespace dk {

static const Col TXT_DIM = hex(0x9a94b4), INK = hex(0x1c1228), EYE = hex(0xfbf8ff);
static const Col BODY_TOP = hex(0x86e3c9), BODY_BOT = hex(0x3aa98f), CHEEK = hex(0xff9ec0);
static const Col ICON_BG = hex(0x1d2540), ICON_RIM = hex(0x2e3a62), GOLD = hex(0xffd166);
static constexpr float ICON_R = 30;
static constexpr float BUBBLE_Y = 22, BUBBLE_H = 42;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float approach(float v, float t, float rate, float dt) { return v + (t - v) * std::fmin(1.f, rate * dt); }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

static void heart(Scene& s, float x, float y, float r, Col c, float a) {
  s.circle(x - r * 0.5f, y - r * 0.2f, r * 0.55f, c, a);
  s.circle(x + r * 0.5f, y - r * 0.2f, r * 0.55f, c, a);
  s.capsule(x - r * 0.9f, y + r * 0.05f, x, y + r * 0.95f, r * 0.28f, c, a);
  s.capsule(x + r * 0.9f, y + r * 0.05f, x, y + r * 0.95f, r * 0.28f, c, a);
  s.circle(x, y + r * 0.25f, r * 0.5f, c, a);
}

Home::Home(Synth& synth, Store& store) : synth_(synth), store_(store) {}

float Home::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return (seed_ & 0xFFFFFF) / float(0x1000000);
}


void Home::iconPos(int i, int n, float& x, float& y) {
  // erste Reihe drei, zweite Reihe der Rest, jeweils mittig
  const int row = i < 3 ? 0 : 1, inRow = row ? n - 3 : (n < 3 ? n : 3), k = row ? i - 3 : i;
  x = W / 2 + (k - (inRow - 1) * 0.5f) * 112;
  y = row ? 372 : 268;
}

int Home::cardsWaiting() const {
  const int tot = store_.get("alb_tot", 0), earned = starsGet(store_) / 3;
  return (earned < tot ? earned : tot) - store_.get("alb_open", 0);
}

void Home::say(const char* text, float secs) {
  snprintf(bubble_, sizeof bubble_, "%s", text);
  bubbleT_ = bubbleDur_ = secs;
  bubbleAsk_ = false;
}

void Home::enter(uint32_t ms) {
  if (!lastSeen_ || ms - lastSeen_ > 30000) { say("Hallo!", 2.2f); idle_ = 0; }   // nach längerer Pause begrüßen
  else { bubbleT_ = 0; idle_ = 4; }
  sqv_ = 2.5f;   // kleiner Hüpfer
}

void Home::tick(const TileInput& in) {
  const float dt = in.dt;
  clock_ += dt;
  idle_ += dt;
  lastSeen_ = in.ms;
  bubbleT_ = std::fmax(0, bubbleT_ - dt);
  giggle_ = std::fmax(0, giggle_ - dt);
  dizzy_ = std::fmax(0, dizzy_ - dt);
  startle_ = std::fmax(0, startle_ - dt * 2);

  // nach der Begrüßung und dann ab und zu: „Wie geht's dir?“
  if (bubbleT_ <= 0 && checkin_ >= 0 && (idle_ > 3 && idle_ < 3.1f + dt)) { say("Wie geht's dir?", 8); bubbleAsk_ = true; }
  if (bubbleT_ <= 0 && checkin_ >= 0 && idle_ > 45) { say("Wie geht's dir?", 8); bubbleAsk_ = true; idle_ = 3.2f; }

  // ---- Antippen ----
  if (in.tap) {
    idle_ = 3.2f;
    const float bw = Scene::textW(fonts::body, bubble_) + 36;
    if (bubbleT_ > 0 && bubbleAsk_ && in.tapY < BUBBLE_Y + BUBBLE_H + 8 && std::fabs(in.tapX - W / 2) < bw / 2 + 8) {
      requestGoto(checkin_);
      bubbleT_ = 0;
      return;
    }
    const float dx = in.tapX - BUDDY_X, dy = in.tapY - BUDDY_Y;
    if (dx * dx + dy * dy < (BUDDY_R + 14) * (BUDDY_R + 14)) {
      giggle_ = 1.1f;
      sqv_ += 3;
      say(rnd() < 0.5f ? "Hihi!" : "Das kitzelt!", 1.6f);
      for (int k = 0; k < 3; k++) { Synth::Voice v; v.wave = Synth::TRI; v.f0 = v.f1 = 520.f + 130 * k; v.delay = 0.08f * k; v.dur = 0.1f; v.vol = 0.12f; synth_.play(v); }
    } else {
      for (int i = 0; i < nApps_; i++) {
        float x, y;
        iconPos(i, nApps_, x, y);
        if ((in.tapX - x) * (in.tapX - x) + (in.tapY - y) * (in.tapY - y) < (ICON_R + 14) * (ICON_R + 14)) { requestGoto(apps_[i].tile); break; }
      }
    }
  }

  // ---- Streicheln: Finger zieht über Grummel ----
  onBuddy_ = false;
  if (in.finger) {
    const float dx = in.fingerX - BUDDY_X, dy = in.fingerY - BUDDY_Y;
    onBuddy_ = dx * dx + dy * dy < (BUDDY_R * 1.5f) * (BUDDY_R * 1.5f);
    if (onBuddy_ && hadFinger_) {
      const float mx = in.fingerX - lastFX_, my = in.fingerY - lastFY_, d = std::sqrt(mx * mx + my * my);
      if (d > 1.2f) {
        strokeAcc_ += d;
        purr_ = approach(purr_, 1, 3, dt);
        sqv_ += d * 0.02f;
        if (strokeAcc_ > 70 && nHearts_ < 12) {   // Herzchen steigen auf
          strokeAcc_ = 0;
          hearts_[nHearts_++] = {BUDDY_X + (rnd() - 0.5f) * BUDDY_R * 1.4f, BUDDY_Y - BUDDY_R * 0.6f, 0};
          Synth::Voice v; v.wave = Synth::NOISE; v.filt = Synth::LP; v.cut0 = v.cut1 = 220; v.amF = 24; v.amD = 0.8f; v.attack = 0.05f; v.dur = 0.5f; v.vol = 0.2f; synth_.play(v);
          if (bubbleT_ <= 0 && rnd() < 0.35f) say(rnd() < 0.5f ? "Mmmh, schön!" : "Das mag ich!", 1.8f);
        }
      }
    }
    lastFX_ = in.fingerX; lastFY_ = in.fingerY;
    idle_ = 3.2f;
  }
  hadFinger_ = in.finger;
  if (!onBuddy_) purr_ = approach(purr_, 0, 1.2f, dt);

  // ---- Schütteln, Klopfen ----
  if (in.shaking) {
    if (dizzy_ < 0.5f) { say("Huiii!", 1.5f); Synth::Voice v; v.wave = Synth::SINE; v.f0 = 900; v.f1 = 300; v.dur = 0.5f; v.vol = 0.15f; synth_.play(v); }
    dizzy_ = 1.6f;
    sqv_ += (rnd() - 0.5f) * 6 * in.shake;
    idle_ = 3.2f;
  }
  if (in.nKnocks > 0 && dizzy_ <= 0) { startle_ = 1; sqv_ -= 2; if (bubbleT_ <= 0) say("Huch!", 1.2f); }

  // ---- Stimme: Grummel hört zu ----
  if (in.voice > 0.15f) { voiceT_ += dt; listen_ = approach(listen_, 1, 6, dt); idle_ = 3.2f; }
  else {
    if (voiceT_ > 0.8f && bubbleT_ <= 0) say(rnd() < 0.5f ? "Ich hör dir zu!" : "Erzähl mir mehr!", 2);
    voiceT_ = 0;
    listen_ = approach(listen_, 0, 2, dt);
  }

  // Blick: zum Finger, sonst umherschauen; Blinzeln; Feder
  float tx = std::sin(clock_ * 0.37f) * 0.5f, ty = std::sin(clock_ * 0.23f) * 0.3f;
  if (in.finger) { tx = clampf((in.fingerX - BUDDY_X) / 120, -1, 1); ty = clampf((in.fingerY - BUDDY_Y) / 120, -1, 1); }
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

void Home::drawBuddy(Scene& s) {
  const float breathe = 1 + 0.02f * std::sin(clock_ * 1.6f);
  const float lean = listen_ * 6;
  const float x = BUDDY_X + lean * 0.3f, y = BUDDY_Y - 4 * smooth(giggle_) - startle_ * 6;
  const float rx = BUDDY_R * (1 + sq_) * breathe, ry = BUDDY_R * (1 - sq_) * breathe;
  s.glow(x, y, BUDDY_R * 1.5f, hex(0x3fc0a0), 0.22f, BUDDY_R * 0.85f);
  s.ellipse(x - rx * 0.38f, y + ry * 0.92f, BUDDY_R * 0.2f, BUDDY_R * 0.1f, scale(BODY_BOT, 0.8f));
  s.ellipse(x + rx * 0.38f, y + ry * 0.92f, BUDDY_R * 0.2f, BUDDY_R * 0.1f, scale(BODY_BOT, 0.8f));
  const float wa = 0.01f + 0.03f * (dizzy_ > 0 ? 1.f : 0.f) + 0.012f * purr_;
  const float wob[6] = {wa, clock_ * 2.1f, wa * 0.5f, -clock_ * 2.9f + 1, wa * 0.3f, clock_ * 3.7f + 2};
  s.blob(x, y, rx, ry, wob, BODY_TOP, BODY_BOT);
  s.ellipse(x - rx * 0.36f, y - ry * 0.52f, rx * 0.24f, ry * 0.12f, hex(0xffffff), 0.18f);
  const float ch = 1 + 0.5f * purr_;
  s.ellipse(x - rx * 0.56f, y + ry * 0.14f, BUDDY_R * 0.13f * ch, BUDDY_R * 0.08f * ch, CHEEK, 0.4f + 0.3f * purr_);
  s.ellipse(x + rx * 0.56f, y + ry * 0.14f, BUDDY_R * 0.13f * ch, BUDDY_R * 0.08f * ch, CHEEK, 0.4f + 0.3f * purr_);
  // Augen
  const float e = BUDDY_R * 0.19f;
  for (int sd = -1; sd <= 1; sd += 2) {
    const float ex = x + sd * rx * 0.33f, ey = y - ry * 0.12f;
    if (dizzy_ > 0) {   // Kringelaugen
      const float rot = clock_ * 8 * sd;
      s.arc(ex, ey, e * 0.8f, 3, rot, rot + 4.5f, INK);
      s.arc(ex, ey, e * 0.38f, 3, rot + 2, rot + 6, INK);
    } else if (giggle_ > 0) {
      s.arc(ex, ey + e * 0.35f, e * 0.75f, 4.5f, -1.15f, 1.15f, INK);
    } else if (purr_ > 0.4f) {
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
  if (giggle_ > 0) {
    s.ellipse(x, my, BUDDY_R * 0.16f, BUDDY_R * 0.13f, INK);
    s.ellipse(x, my + BUDDY_R * 0.05f, BUDDY_R * 0.09f, BUDDY_R * 0.05f, hex(0xe0607e));
  } else if (dizzy_ > 0) {
    for (int k = 0; k < 4; k++) s.capsule(x - BUDDY_R * 0.2f + k * BUDDY_R * 0.1f, my + (k & 1 ? 3 : -3), x - BUDDY_R * 0.1f + k * BUDDY_R * 0.1f, my + (k & 1 ? -3 : 3), 1.8f, INK);
  } else if (listen_ > 0.4f) {
    s.ellipse(x, my, BUDDY_R * 0.06f, BUDDY_R * 0.08f, INK);
    for (int k = 1; k <= 2; k++)   // Schallwellen am Ohr
      s.arc(x + rx + 4, y - ry * 0.1f, 8.f * k, 2.5f, 0.6f, 2.5f, hex(0x8fe3d0), (0.7f - 0.2f * k) * listen_);
  } else {
    s.arc(x, my - BUDDY_R * 0.1f, BUDDY_R * 0.13f, 3.5f, PIF - 0.95f, PIF + 0.95f, INK);
  }
  for (int i = 0; i < nHearts_; i++) {
    const Heart& h = hearts_[i];
    heart(s, h.x + 6 * std::sin(h.t * 5 + i), h.y - 50 * h.t, 9, hex(0xff8fb8), 1 - smooth((h.t - 0.8f) / 0.6f));
  }
}

void Home::drawIcon(Scene& s, const AppIcon& a, float x, float y) {
  s.circle(x, y, ICON_R + 2, ICON_RIM);
  s.circle(x, y, ICON_R, ICON_BG);
  switch (a.kind) {
    case 0: drawFace(s, CheckIn::FROH, x, y, 22, clock_); break;
    case 1: {   // kleiner Grummel mit Wutfunken
      s.ellipse(x, y + 3, 19, 17, BODY_TOP, BODY_BOT);
      s.circle(x - 6, y, 3.5f, EYE); s.circle(x + 6, y, 3.5f, EYE);
      s.circle(x - 6, y, 1.8f, INK); s.circle(x + 6, y, 1.8f, INK);
      for (int k = 0; k < 3; k++) {
        const float an = clock_ * 1.5f + k * 2.1f;
        s.glow(x + 24 * std::sin(an), y - 20 * std::cos(an), 7, hex(0xff5a2c), 0.7f);
      }
      break;
    }
    case 2:   // Ballon
      s.capsule(x, y + 15, x + 2, y + 26, 1, hex(0xd8d0e8));
      s.ellipse(x, y - 2, 15, 18, hex(0xffb8cc), hex(0xe8668e));
      s.ellipse(x - 5, y - 9, 4, 3, hex(0xffffff), 0.4f);
      break;
    case 3:   // Kugel und Loch
      s.circle(x + 7, y + 6, 11, hex(0x02040a));
      s.ring(x + 7, y + 6, 12, 2, hex(0x3a4a70));
      s.ellipse(x - 9, y - 7, 9, 9, hex(0xffdf80), hex(0xe8a020));
      break;
    default: {   // Pferd
      static const Horse H = {"", 150, Coat::CHESTNUT, 0xc07a3a, 0xf0dca0, false};
      drawHorse(s, H, x - 2, y + 20, 0.36f, clock_ * 0.3f);
      if (cardsWaiting() > 0) {   // neue Karte wartet
        const float p = 0.8f + 0.2f * std::sin(clock_ * 4);
        s.circle(x + ICON_R * 0.75f, y - ICON_R * 0.75f, 11 * p, GOLD);
        s.text(fonts::small, "!", x + ICON_R * 0.75f, y - ICON_R * 0.75f - 11, hex(0x2a1a00), 1, Align::Center);
      }
      break;
    }
  }
  s.text(fonts::small, a.label, x, y + ICON_R + 4, TXT_DIM, 1, Align::Center);
}

void Home::draw(Scene& s) {
  // Sprechblase
  if (bubbleT_ > 0 && bubble_[0]) {
    const float a = std::fmin(1.f, bubbleT_ / 0.3f) * std::fmin(1.f, (bubbleDur_ - bubbleT_) / 0.25f);
    const float w = Scene::textW(fonts::body, bubble_) + 36;
    const Col bg = bubbleAsk_ ? hex(0xffe9a8) : hex(0xf2eefa);
    s.rrect(W / 2 - w / 2, BUBBLE_Y, w, BUBBLE_H, BUBBLE_H / 2, bg, a);
    s.circle(W / 2 + 12, BUBBLE_Y + BUBBLE_H + 6, 6, bg, a);
    s.circle(W / 2 + 6, BUBBLE_Y + BUBBLE_H + 16, 3.5f, bg, a);
    s.text(fonts::body, bubble_, W / 2, BUBBLE_Y + (BUBBLE_H - fonts::body.lineH) / 2 + 1, hex(0x2a1a30), a, Align::Center);
  }
  drawBuddy(s);
  drawStarCount(s, W / 2, 196, starsGet(store_), 0.8f);
  for (int i = 0; i < nApps_; i++) {
    float x, y;
    iconPos(i, nApps_, x, y);
    drawIcon(s, apps_[i], x, y);
  }
}

}  // namespace dk
