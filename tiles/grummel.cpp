#include "grummel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"
#include "quiz.h"
#include "rewards.h"

namespace dk {

const GrummelSkin OFFICE_SKIN = {
    "Dampf ablassen",
    hex(0x8f7fe8), hex(0x5546b4),   // zufrieden: Flieder
    hex(0x4a3a74), hex(0x241a42),   // voller Wut: dunkles Violett
    hex(0xff8fb8), hex(0x6b5bd6), hex(0x9a8cff),
    hex(0x2a1a36), hex(0xa03070), hex(0x7a4a8e),
    "Klopf auf den Tisch oder schüttel mich",
    "Danke fürs Füttern",
    "Atme mit mir", nullptr, nullptr,
    3, 4.f, 6.f, false,
};

// Für Kinder: Minzgrün, die Wut als rote Funken, Atmen mit Blume und Kerze, danach ein Stern
const GrummelSkin KID_SKIN = {
    "Grummel",
    hex(0x86e3c9), hex(0x3aa98f),   // zufrieden: Minze
    hex(0x8a4060), hex(0x4a1c34),   // voller Wut: dunkles Beerenrot, nie bedrohlich
    hex(0xff9ec0), hex(0x3fc0a0), hex(0xffb070),
    hex(0x6a1a14), hex(0xff5a2c), hex(0xd0482c),
    "Tippe ganz schnell auf mich!",
    "Mmh, lecker Wut!",
    "Jetzt atmen wir", "Riech an der Blume", "Puste die Kerze aus",
    4, 4.f, 5.f, true,
};

static constexpr float CX = W / 2, CY = 214;           // Mitte von Grummel und Füllring
static constexpr float R0 = 60, RMAX = 100, RING = 150;
static constexpr float FULL_DUR = 2.6f, TRANSFORM_DUR = 3.8f, INTRO = 2.2f, THANKS_DUR = 4.8f;
static const float SENS[3] = {0.05f, 0.10f, 0.20f};
static const char* const SENS_NAMES[4] = {"fein", "mittel", "fest", "eingemessen"};
static constexpr float CAL_TYPE = 10, CAL_KNOCK_MAX = 20, CAL_RESULT = 6;
static constexpr int CAL_KNOCKS = 6;

static const Col TXT = hex(0xe8e4f4), TXT_DIM = hex(0x9a94b4), INK = hex(0x1c1228), EYE = hex(0xfbf8ff);
static const Col DARK_HI = hex(0x5c4070);
static const Col CALM = hex(0x8fe3d0);
static const Col PASTEL[5] = {hex(0x8ff0c8), hex(0xffe58a), hex(0xffb3d1), hex(0x9fd8ff), hex(0xd6c2ff)};

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float approach(float v, float t, float rate, float dt) { return v + (t - v) * std::fmin(1.f, rate * dt); }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

Grummel::Grummel(Synth& synth, Store& store, uint32_t seed, const GrummelSkin& skin)
    : synth_(synth), store_(store), skin_(skin), seed_(seed | 1) {
  thrCustom_ = store_.get("gr_thr", 0) / 1000.f;
  sens_ = store_.get("gr_sens", 1) % 4;
  if (sens_ == 3 && thrCustom_ <= 0) sens_ = 1;
  meter_ = store_.get("gr_dbg", 0) != 0;
  allN_ = store_.get("gr_all", 0);
  wkId_ = store_.get("gr_wk", -1);
  wkN_ = store_.get("gr_wkn", 0);
}

float Grummel::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return (seed_ & 0xFFFFFF) / float(0x1000000);
}

float Grummel::motionThreshold() const { return sens_ == 3 ? thrCustom_ : SENS[sens_]; }

bool Grummel::keepAwake() const { return phase_ != DOZE && phase_ != READY; }

// ---------------- Einmessen ----------------

void Grummel::tickCalib(const TileInput& in) {
  if (calStep_ == 0) {   // Tippen: Ausschläge je Frame sammeln
    if (calN_ < 320) calPeaks_[calN_++] = in.framePeak;
    if (t_ >= CAL_TYPE) {
      std::sort(calPeaks_, calPeaks_ + calN_);
      calTyp_ = std::fmax(0.004f, calN_ ? calPeaks_[int(calN_ * 0.99f)] : 0.f);   // 99. Perzentil
      calStep_ = 1;
      t_ = 0;
    }
  } else if (calStep_ == 1) {   // Klopfen: deutlich über dem Tippen, ein Schlag = Spitze innerhalb von 150 ms
    const float pthr = std::fmax(calTyp_ * 1.5f, 0.015f);
    if (in.framePeak > pthr) {
      if (calLast_ >= 0 && clock_ - calLast_ < 0.15f && calKn_ > 0) calKnock_[calKn_ - 1] = std::fmax(calKnock_[calKn_ - 1], in.framePeak);
      else if (calKn_ < CAL_KNOCKS) { calKnock_[calKn_++] = in.framePeak; calLast_ = clock_; sqv_ -= 2; sMunch(); }
    }
    if ((calKn_ >= CAL_KNOCKS && clock_ - calLast_ > 0.4f) || t_ > CAL_KNOCK_MAX) {
      calOk_ = false;
      if (calKn_ >= 3) {
        std::sort(calKnock_, calKnock_ + calKn_);
        const float kmin = calKnock_[calKn_ >= 5 ? 1 : 0];   // zweitschwächster Schlag, ein Ausreißer zählt nicht
        if (kmin > calTyp_ * 1.5f) {
          // geometrisches Mittel zwischen Tippen und Klopfen, mit Abstand zu beiden
          calThr_ = clampf(std::sqrt(calTyp_ * kmin), calTyp_ * 1.4f, kmin * 0.6f);
          thrCustom_ = calThr_;
          sens_ = 3;
          store_.set("gr_thr", int(calThr_ * 1000 + 0.5f));
          store_.set("gr_sens", 3);
          calOk_ = true;
        }
      }
      calStep_ = 2;
      t_ = 0;
      if (calOk_) sRise();
    }
  } else if (t_ > CAL_RESULT) {
    setPhase(READY);
  }
}

static void gstr(char* buf, int n, const char* fmt, float v) {   // Zahl mit Komma
  snprintf(buf, n, fmt, v);
  for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
}

void Grummel::drawCalib(Scene& s) {
  const float ty = 368;
  char buf[64];
  if (calStep_ == 0) {
    const float p = clampf(t_ / CAL_TYPE, 0, 1);
    s.arc(CX, CY, RING, 6, p * TAU, TAU, hex(0x2a2440));
    if (p > 0) s.arc(CX, CY, RING, 7, 0, p * TAU, hex(0x8fe3d0));
    s.text(fonts::body, "Tippe ganz normal", W / 2, ty, TXT, 1, Align::Center);
    s.text(fonts::small, "auf der Tastatur …", W / 2, ty + 32, TXT_DIM, 1, Align::Center);
  } else if (calStep_ == 1) {
    s.ring(CX, CY, RING, 6, hex(0x2a2440));
    for (int i = 0; i < CAL_KNOCKS; i++) {
      const float a = (i + 0.5f) * TAU / CAL_KNOCKS;
      s.circle(CX + RING * std::sin(a), CY - RING * std::cos(a), i < calKn_ ? 9.f : 5.f, i < calKn_ ? hex(0xff9a5a) : hex(0x4a4466));
    }
    snprintf(buf, sizeof buf, "Jetzt %d× klopfen", CAL_KNOCKS - calKn_ > 0 ? CAL_KNOCKS - calKn_ : 0);
    s.text(fonts::body, buf, W / 2, ty, TXT, 1, Align::Center);
    s.text(fonts::small, "neben mir auf den Tisch", W / 2, ty + 32, TXT_DIM, 1, Align::Center);
  } else {
    if (calOk_) {
      gstr(buf, sizeof buf, "Schwelle %.3f g", calThr_);
      s.text(fonts::body, buf, W / 2, ty, TXT, 1, Align::Center);
      char b2[32];
      gstr(b2, sizeof b2, "%.3f", calTyp_);
      snprintf(buf, sizeof buf, "Tippen bis %s · Klopfen ab ", b2);
      gstr(b2, sizeof b2, "%.2f g", calKnock_[calKn_ >= 5 ? 1 : 0]);
      strncat(buf, b2, sizeof buf - strlen(buf) - 1);
      s.text(fonts::small, buf, W / 2, ty + 32, TXT_DIM, 1, Align::Center);
    } else {
      s.text(fonts::body, calKn_ < 3 ? "Kein Klopfen erkannt" : "Zu leise geklopft", W / 2, ty, TXT, 1, Align::Center);
      s.text(fonts::small, "Schwelle bleibt wie sie war", W / 2, ty + 32, TXT_DIM, 1, Align::Center);
    }
  }
}

void Grummel::settingText(int i, char* buf, int n) const {
  if (i == 0) snprintf(buf, n, "Klopfen: %s", SENS_NAMES[sens_]);
  else if (i == 1) snprintf(buf, n, "Klopfen einmessen");
  else snprintf(buf, n, "Messwerte: %s", meter_ ? "an" : "aus");
}

bool Grummel::settingTap(int i) {
  if (i == 0) {
    sens_ = (sens_ + 1) % (thrCustom_ > 0 ? 4 : 3);
    store_.set("gr_sens", sens_);
    return false;
  }
  if (i == 1) {   // Einmessen starten, Einstellungen schließen
    nDark_ = 0;
    energy_ = 0;
    calStep_ = 0; calN_ = 0; calKn_ = 0; calLast_ = -1;
    setPhase(CALIB);
    return true;
  }
  meter_ = !meter_;
  store_.set("gr_dbg", meter_);
  return false;
}

void Grummel::enter(uint32_t) {
  if (phase_ == DOZE) setPhase(READY);
  hintT_ = 0;
}

void Grummel::setPhase(Phase p) {
  phase_ = p;
  t_ = 0;
  if (p == CHARGE) {
    for (int i = 0; i < nDark_; i++) dark_[i].homing = true;   // die schwebenden Partikel werden eingesaugt
  }
  if (p == FULL) countSession(epoch_);
  if (p == THANKS && skin_.kid) stars_ = starsAdd(store_);
  if (p == BREATHE) { breathN_ = 0; breath_ = 0; }
}

// ---------------- Statistik ----------------

static int weekOf(int64_t epoch) { return int((epoch / 86400 + 3) / 7); }   // Wochen ab Montag, 1.1.1970 war ein Donnerstag

void Grummel::countSession(int64_t epoch) {
  store_.set("gr_all", ++allN_);
  if (epoch > 0) {
    const int wk = weekOf(epoch);
    if (wk != wkId_) { wkId_ = wk; wkN_ = 0; store_.set("gr_wk", wk); }
    store_.set("gr_wkn", ++wkN_);
  }
}

void Grummel::statsText(char* buf, int n) const {
  if (epoch_ > 0) snprintf(buf, n, "Diese Woche %d× Dampf abgelassen", weekOf(epoch_) == wkId_ ? wkN_ : 0);
  else snprintf(buf, n, "Schon %d× Dampf abgelassen", allN_);
}

// ---------------- Partikel ----------------

void Grummel::spawnDark(float ang, float val, bool homing) {
  if (nDark_ >= 96) return;
  Dark& d = dark_[nDark_++];
  const float dist = homing ? 250.f : 115.f + rnd() * 60.f;
  d.x = CX + dist * std::sin(ang);
  d.y = CY - dist * std::cos(ang);
  // seitlicher Schwung, damit sie im Bogen hereinfliegen
  const float sw = (rnd() < 0.5f ? -1.f : 1.f) * (80 + rnd() * 90);
  d.vx = std::cos(ang) * sw - std::sin(ang) * 60;
  d.vy = std::sin(ang) * sw + std::cos(ang) * 60;
  d.r = 4.5f + rnd() * 5.5f;
  d.val = val;
  d.ph = ang;
  d.homing = homing;
}

void Grummel::spawnLight() {
  if (nLight_ >= 96) return;
  Light& l = light_[nLight_++];
  const float R = R_;
  l.kind = uint8_t(rnd() * 3);
  l.x = CX + driftX_ + (rnd() - 0.5f) * R * 1.2f;
  l.y = CY + driftY_ - R * 0.5f + rnd() * R * 0.3f;
  l.vx = (rnd() - 0.5f) * 50;
  l.vy = -(55 + rnd() * 75);
  l.r = l.kind == 0 ? 6 + rnd() * 6 : 4 + rnd() * 4;
  l.life = 0;
  l.max = 2.4f + rnd() * 1.6f;
  l.ph = rnd() * TAU;
  l.c = PASTEL[int(rnd() * 5) % 5];
}

void Grummel::feed(float strength, float dirX, float dirY, bool fromShake) {
  if (phase_ == FULL || phase_ == TRANSFORM || phase_ == BREATHE || phase_ == THANKS) return;
  if (phase_ == DOZE) { startle_ = 1; sqv_ -= 3; sHuh(); }
  if (phase_ != CHARGE) setPhase(CHARGE);
  lastFeed_ = clock_;
  if (fromShake) {
    spawnDark(rnd() * TAU, 0.010f, true);
    return;
  }
  const float bonus = clock_ - lastKnock_ < 0.35f ? 1.25f : 1.f;   // schnelles Trommeln bringt mehr
  lastKnock_ = clock_;
  const int n = 2 + int(strength * 5 + 0.5f);
  const bool hasDir = dirX != 0 || dirY != 0;
  const float base = hasDir ? std::atan2(dirX, dirY) : rnd() * TAU;
  for (int i = 0; i < n; i++) spawnDark(base + (rnd() - 0.5f) * (hasDir ? 1.4f : TAU), 0.011f * bonus, true);
  sqv_ -= 1.2f + 1.5f * strength;   // zuckt zusammen
}

void Grummel::feedTap(float x, float y, float strength) {
  const bool wasDoze = phase_ == DOZE;
  const float before = lastKnock_;
  feed(0, 0, 0, true);   // weckt, schaltet aufs Laden und merkt die Zeit; ein Partikel von irgendwo
  if (nDark_ > 0) nDark_--;   // den Zufallspartikel wieder weg, die Funken kommen vom Finger
  const float bonus = clock_ - before < 0.35f ? 1.3f : 1.f;   // schnelles Tippen bringt mehr
  lastKnock_ = clock_;
  const int n = 3 + int(strength * 3 + 0.5f);
  for (int i = 0; i < n && nDark_ < 96; i++) {
    Dark& d = dark_[nDark_++];
    const float an = rnd() * TAU, sp = 80 + rnd() * 120;
    d.x = x + (rnd() - 0.5f) * 16;
    d.y = y + (rnd() - 0.5f) * 16;
    d.vx = std::sin(an) * sp;
    d.vy = -std::cos(an) * sp;
    d.r = 4.5f + rnd() * 4.5f;
    d.val = 0.011f * bonus;
    d.ph = an;
    d.homing = true;
  }
  if (!wasDoze) sqv_ -= 0.8f + strength;
}

// ---------------- Logik ----------------

void Grummel::tick(const TileInput& in) {
  const float dt = in.dt;
  clock_ += dt;
  t_ += dt;
  epoch_ = in.epoch;
  thr_ = in.threshold;
  peaks_[peakI_] = in.framePeak;
  peakI_ = (peakI_ + 1) % 64;

  if (phase_ == CALIB) tickCalib(in);
  else for (int i = 0; i < in.nKnocks; i++) feed(in.knocks[i].strength, in.knocks[i].dirX, in.knocks[i].dirY, false);
  if (in.shaking && phase_ != CALIB) {   // 12 bis 40 Partikel pro Sekunde, satt nach etwa 2,5 bis 5 s
    shakeAcc_ += dt * (12 + 28 * in.shake);
    while (shakeAcc_ >= 1) { feed(in.shake, 0, 0, true); shakeAcc_ -= 1; }
    if (phase_ == CHARGE) sqv_ += (rnd() - 0.5f) * 8 * in.shake;   // schwabbelt
  }
  jitter_ = approach(jitter_, in.shaking ? in.shake : 0.f, 8, dt);

  if (in.tap && skin_.kid && (phase_ == DOZE || phase_ == READY || phase_ == CHARGE)) {
    // Kinder füttern Grummel durch Tippen: Tempo zählt, ein kräftiger Tipp (Ruck in der IMU) etwas mehr
    feedTap(in.tapX, in.tapY, clampf((in.tapPeak - 0.05f) / 0.5f, 0, 1));
  } else if (in.tap) {
    if (phase_ == CALIB) setPhase(READY);             // Einmessen abbrechen
    else if (phase_ == BREATHE) setPhase(THANKS);     // Ruhephase abbrechen
    else if (phase_ == DOZE) { setPhase(READY); sHuh(); sqv_ -= 2; }
    else if (phase_ == READY) { pokeT_ = 0.8f; sqv_ += 2.5f; t_ = 0; }   // kichert
  }

  // ---- Phasen ----
  int homing = 0;
  for (int i = 0; i < nDark_; i++) homing += dark_[i].homing;
  switch (phase_) {
    case DOZE:
      if (nDark_ < 7 && rnd() < dt * 0.5f) spawnDark(rnd() * TAU, 0, false);
      zT_ += dt;
      if (zT_ > 2.4f && nLight_ < 96) {   // „z“ steigt auf
        zT_ = 0;
        Light& l = light_[nLight_++];
        l.kind = 3; l.x = CX + driftX_ + R_ * 0.55f; l.y = CY + driftY_ - R_ * 0.7f;
        l.vx = 8; l.vy = -16; l.r = 1; l.life = 0; l.max = 3.2f; l.ph = rnd() * TAU; l.c = hex(0xb9b0e0);
      }
      break;
    case READY:
      if (nDark_ < 8 && rnd() < dt * 0.8f) spawnDark(rnd() * TAU, 0, false);
      energy_ = std::fmax(0, energy_ - dt * 0.4f);
      if (t_ > 25) setPhase(DOZE);
      break;
    case CHARGE:
      if (energy_ >= 1) setPhase(FULL);
      else if (clock_ - lastFeed_ > 3 && homing == 0) {
        if (energy_ >= 0.2f) setPhase(FULL);
        else setPhase(READY);   // nur genascht
      }
      break;
    case FULL:
      if (burpT_ < 0 && t_ > 1.0f) { burpT_ = 0; sBurp(); sqv_ += 4; }
      if (t_ > FULL_DUR) { setPhase(TRANSFORM); sRise(); }
      break;
    case TRANSFORM: {
      const float before = energy_;
      energy_ = std::fmax(0, energy_ - dt / 3.0f);
      lightAcc_ += before > 0 ? dt * 16 : 0;
      while (lightAcc_ >= 1) { spawnLight(); lightAcc_ -= 1; }
      if (t_ > TRANSFORM_DUR) setPhase(BREATHE);
      break;
    }
    case BREATHE: {
      const float L = skin_.inhale + skin_.exhale;
      const float k = t_ - INTRO;
      if (k < 0) { breath_ = approach(breath_, 0, 3, dt); break; }
      breathN_ = int(k / L);
      if (breathN_ >= skin_.breaths) { setPhase(THANKS); break; }
      const float u = k - breathN_ * L;
      breath_ = u < skin_.inhale ? 0.5f - 0.5f * std::cos(PIF * u / skin_.inhale) : 0.5f + 0.5f * std::cos(PIF * (u - skin_.inhale) / skin_.exhale);
      break;
    }
    case THANKS:
      breath_ = approach(breath_, 0, 2, dt);
      if (t_ > THANKS_DUR) {
        setPhase(DOZE);
        if (skin_.kid && home_ >= 0) requestGoto(home_);   // „Und wie fühlt sich dein Bauch jetzt an?“
      }
      break;
    case CALIB:
      break;
  }
  if (burpT_ >= 0) { burpT_ += dt; if (burpT_ > 1.6f) burpT_ = -1; }
  if (phase_ != FULL && burpT_ < 0) burpT_ = -1;
  pokeT_ = std::fmax(0, pokeT_ - dt);
  startle_ = std::fmax(0, startle_ - dt * 2);
  munchCd_ = std::fmax(0, munchCd_ - dt);
  hintT_ += dt;

  // ---- Körper ----
  eVis_ = approach(eVis_, energy_, 4, dt);
  float targetR = R0 + (RMAX - R0) * eVis_;
  R_ = approach(R_, targetR, 6, dt);
  // Feder für Quetschen und Strecken
  sqv_ += (-90.f * sq_ - 7.f * sqv_) * dt;
  sq_ += sqv_ * dt;
  sq_ = clampf(sq_, -0.16f, 0.16f);
  chew_ = std::fmax(0, chew_ - dt * 1.5f);

  // Ausdruck
  float tEye = 1, tHappy = 0, tPeace = 0, tMouth = 0, tPuff = 0, tLean = 0;
  switch (phase_) {
    case DOZE: tEye = 0; tPeace = 1; break;
    case READY: tHappy = pokeT_ > 0 ? 1.f : 0.f; break;
    case CHARGE: tEye = 0.8f - 0.4f * chew_; tMouth = chew_ * std::fabs(std::sin(clock_ * 13)); break;
    case FULL:
      if (t_ < 1.0f) { tHappy = 1; tPuff = smooth(t_ / 0.45f); }
      else if (t_ < 1.6f) { tHappy = 1; tMouth = 1; tPuff = 0.3f; }
      else { tHappy = 1; tLean = 1; }
      break;
    case TRANSFORM: tHappy = t_ < 1.5f ? 1.f : 0.f; tLean = t_ < 1.2f ? 1.f : 0.f; break;
    case BREATHE: tPeace = 1; break;
    case THANKS:
      if (t_ < 1.6f) tHappy = 1;
      else if (t_ < 3.2f) { tMouth = 0.75f * smooth((t_ - 1.6f) / 0.6f) * (1 - smooth((t_ - 2.6f) / 0.6f)); tEye = 0.3f; }   // gähnt
      else tPeace = 1;
      break;
    case CALIB: break;
  }
  if (startle_ > 0) { tEye = 1.3f; tPeace = 0; }
  eyeOpen_ = approach(eyeOpen_, tEye, 10, dt);
  happy_ = approach(happy_, tHappy, 12, dt);
  peace_ = approach(peace_, tPeace, 6, dt);
  mouthOpen_ = approach(mouthOpen_, tMouth, 18, dt);
  puff_ = approach(puff_, tPuff, 8, dt);
  lean_ = approach(lean_, tLean, 4, dt);

  // Blinzeln, Umschauen, kleine Hüpfer
  blinkT_ -= dt;
  if (blinkT_ < -0.14f) blinkT_ = 2.5f + rnd() * 4;
  lookT_ -= dt;
  if (lookT_ < 0) { lookT_ = 1 + rnd() * 2.5f; lookTX_ = (rnd() - 0.5f) * 1.6f; lookTY_ = (rnd() - 0.5f) * 1.0f; }
  if (phase_ == CHARGE || phase_ == FULL) {
    // auf den nächsten anfliegenden Partikel schauen
    float best = 1e9f;
    for (int i = 0; i < nDark_; i++) {
      const float dx = dark_[i].x - (CX + driftX_), dy = dark_[i].y - (CY + driftY_), d = dx * dx + dy * dy;
      if (dark_[i].homing && d < best) { best = d; const float l = std::sqrt(d) + 1; lookTX_ = dx / l; lookTY_ = dy / l; }
    }
  }
  if (phase_ == TRANSFORM) { lookTX_ = 0; lookTY_ = -1; }
  if (jitter_ > 0.1f) {   // Kulleraugen beim Schütteln
    lookTX_ = std::sin(clock_ * 17) * jitter_;
    lookTY_ = std::cos(clock_ * 13) * jitter_;
  }
  lookX_ = approach(lookX_, lookTX_, jitter_ > 0.1f ? 20.f : 6.f, dt);
  lookY_ = approach(lookY_, lookTY_, jitter_ > 0.1f ? 20.f : 6.f, dt);
  if (phase_ == READY) {
    hopT_ -= dt;
    if (hopT_ < 0) { hopT_ = 6 + rnd() * 6; sqv_ += 2.5f; }
  }
  // langsames Wandern gegen Einbrennen, im Schlaf etwas weiter
  const float far = phase_ == DOZE ? 1.f : 0.f;
  wander_ = approach(wander_, far, 0.3f, dt);
  driftX_ = (10 + 14 * wander_) * std::sin(clock_ * 0.05f);
  driftY_ = (6 + 10 * wander_) * std::sin(clock_ * 0.037f + 1.3f);

  // ---- Partikel bewegen ----
  const float gx = CX + driftX_, gy = CY + driftY_;
  const float mx = gx, my = gy + R_ * 0.3f;
  for (int i = 0; i < nDark_;) {
    Dark& d = dark_[i];
    bool eaten = false;
    if (d.homing) {
      const float dx = mx - d.x, dy = my - d.y, dist = std::sqrt(dx * dx + dy * dy) + 1e-3f;
      const float a = 900 + 1400 * std::fmax(0, 1 - dist / 250);
      d.vx += dx / dist * a * dt;
      d.vy += dy / dist * a * dt;
      const float damp = std::fmax(0, 1 - 2.4f * dt);
      d.vx *= damp; d.vy *= damp;
      d.x += d.vx * dt;
      d.y += d.vy * dt;
      const float cx = d.x - gx, cy = d.y - gy;
      eaten = dist < R_ * 0.3f || cx * cx + cy * cy < (R_ * 0.9f) * (R_ * 0.9f);
    } else {
      d.ph += dt * 0.22f;
      const float rr = 140 + 25 * std::sin(d.ph * 2.3f + d.r);
      const float tx = CX + rr * std::sin(d.ph), ty = CY - rr * std::cos(d.ph);
      d.x = approach(d.x, tx, 1.5f, dt);
      d.y = approach(d.y, ty, 1.5f, dt);
    }
    if (eaten) {
      energy_ = std::fmin(1.f, energy_ + d.val);
      chew_ = 1;
      sqv_ += 0.6f + 30 * d.val;
      if (munchCd_ <= 0) { sMunch(); munchCd_ = 0.13f; }
      dark_[i] = dark_[--nDark_];
      continue;
    }
    i++;
  }
  // Ruhe- und Atemphase: übrige schwebende Partikel ausblenden
  if (phase_ == TRANSFORM || phase_ == BREATHE || phase_ == THANKS)
    for (int i = 0; i < nDark_; i++) dark_[i].homing = true;

  for (int i = 0; i < nLight_;) {
    Light& l = light_[i];
    l.life += dt;
    l.vy -= 12 * dt;
    l.x += (l.vx + std::sin(l.ph + l.life * 2.6f) * 22) * dt;
    l.y += l.vy * dt;
    if (l.life > l.max || l.y < -30) { light_[i] = light_[--nLight_]; continue; }
    i++;
  }
}

// ---------------- Zeichnen ----------------

void Grummel::drawBody(Scene& s) {
  float R = R_ * (1 + 0.12f * puff_);
  if (phase_ == BREATHE || phase_ == THANKS) R *= 1 + 0.16f * breath_;
  if (phase_ == DOZE) R *= 1 + 0.025f * std::sin(clock_ * 1.2f);
  const float jx = jitter_ * 8 * (rnd() - 0.5f), jy = jitter_ * 8 * (rnd() - 0.5f);
  const float gx = CX + driftX_ + jx, gy = CY + driftY_ - 8 * lean_ + jy - startle_ * 10;
  const float rx = R * (1 + sq_), ry = R * (1 - sq_) * (1 + 0.04f * lean_);

  const float dimF = phase_ == DOZE ? 0.78f : 1.f;
  const Col top = scale(mix(skin_.top, skin_.fullTop, eVis_), dimF);
  const Col bottom = scale(mix(skin_.bottom, skin_.fullBottom, eVis_), dimF);

  // Aura
  s.glow(gx, gy, R * 1.5f, mix(skin_.aura, hex(0x4a1f5a), eVis_), 0.26f * dimF, R * 0.82f);
  // Füße
  const Col foot = scale(bottom, 0.8f);
  s.ellipse(gx - rx * 0.38f, gy + ry * 0.9f, R * 0.2f, R * 0.11f, foot);
  s.ellipse(gx + rx * 0.38f, gy + ry * 0.9f, R * 0.2f, R * 0.11f, foot);
  // Körper, wabernd (beim Kauen und Wackeln stärker)
  const float wa = 0.008f + 0.012f * chew_ + 0.05f * std::fabs(sq_) + 0.02f * jitter_;
  const float wob[6] = {wa, clock_ * 2.1f, wa * 0.45f, -clock_ * 2.9f + 1, wa * 0.3f, clock_ * 3.7f + 2};
  s.blob(gx, gy, rx, ry, wob, top, bottom);
  // Glanzlicht oben links
  s.ellipse(gx - rx * 0.36f, gy - ry * 0.52f, rx * 0.24f, ry * 0.12f, hex(0xffffff), (0.16f - 0.08f * eVis_) * dimF);

  // Bäckchen
  const float ch = 1 + 0.6f * puff_;
  s.ellipse(gx - rx * 0.56f, gy + ry * 0.14f, R * 0.13f * ch, R * 0.08f * ch, skin_.cheek, 0.38f + 0.25f * puff_);
  s.ellipse(gx + rx * 0.56f, gy + ry * 0.14f, R * 0.13f * ch, R * 0.08f * ch, skin_.cheek, 0.38f + 0.25f * puff_);

  // Augen
  const float e = R * 0.19f;
  for (int side = -1; side <= 1; side += 2) {
    const float ex = gx + side * rx * 0.33f, ey = gy - ry * 0.12f;
    if (happy_ > 0.5f) {
      s.arc(ex, ey + e * 0.35f, e * 0.75f, 4.5f, -1.15f, 1.15f, INK);            // ^ ^
    } else if (peace_ > 0.5f) {
      s.arc(ex, ey - e * 0.25f, e * 0.75f, 4.f, PIF - 1.15f, PIF + 1.15f, INK);    // ‿ ‿ geschlossen
    } else {
      float open = clampf(eyeOpen_, 0, 1.3f);
      if (blinkT_ < 0) open *= 0.08f;
      if (open < 0.18f) {
        s.capsule(ex - e * 0.75f, ey, ex + e * 0.75f, ey, 2.2f, INK);
      } else {
        const float ery = e * 1.15f * open;
        s.ellipse(ex, ey, e, ery, EYE);
        const float px = ex + lookX_ * e * 0.35f, py = ey + lookY_ * ery * 0.3f;
        const float pr = e * (0.55f + 0.1f * startle_);
        s.ellipse(px, py, pr, std::fmin(pr, ery * 0.85f), INK);
        s.circle(px - pr * 0.35f, py - pr * 0.35f, pr * 0.28f, EYE, 0.9f);
      }
    }
  }

  // Mund
  const float my = gy + ry * 0.32f;
  if (mouthOpen_ > 0.08f) {
    const float wide = phase_ == FULL ? 1.f : 0.f;
    const float mrx = R * (0.1f + 0.05f * mouthOpen_ + 0.07f * wide), mry = R * 0.2f * mouthOpen_;
    s.ellipse(gx, my, mrx, mry, INK);
    if (mouthOpen_ > 0.35f) s.ellipse(gx, my + mry * 0.5f, mrx * 0.6f, mry * 0.35f, hex(0xe0607e), 0.9f);
  } else if (puff_ > 0.5f) {
    s.circle(gx, my, R * 0.05f, INK);   // Backen voll, Mund gespitzt
  } else {
    s.arc(gx, my - R * 0.1f, R * 0.13f, 3.5f, PIF - 0.95f, PIF + 0.95f, INK);
  }
}

void Grummel::draw(Scene& s) {
  const float gx = CX + driftX_, gy = CY + driftY_;

  // Füllring
  const float ringA = (phase_ == CHARGE || phase_ == FULL) ? 1.f : std::fmax(0, 1 - t_ * 2) * (phase_ == TRANSFORM);
  if (ringA > 0.01f && (eVis_ > 0.005f || phase_ == CHARGE)) {
    // Spur nur, wo der Füllbogen nicht liegt; auf Schwarz deckend mit abgedunkelter Farbe statt halbtransparent
    const Col rc = mix(skin_.ring, hex(0xff9a5a), eVis_), track = scale(hex(0x2a2440), 0.7f * ringA);
    const float a1 = eVis_ > 0.004f ? eVis_ * TAU : 0;
    if (a1 < TAU - 0.05f) s.arc(CX, CY, RING, 6, a1, TAU, track);
    if (a1 > 0) {
      s.arc(CX, CY, RING, 7, 0, a1, scale(rc, ringA));
      s.glow(CX + RING * std::sin(a1), CY - RING * std::cos(a1), 16, rc, 0.5f * ringA);
    }
  }

  // Atemkreis
  if (phase_ == BREATHE || (phase_ == THANKS && breath_ > 0.01f)) {
    const float b = breath_, rr = R_ * 1.3f + 58 * b;
    const float a = phase_ == BREATHE ? std::fmin(1.f, t_ / 1.0f) : std::fmax(0, 1 - t_);
    s.glow(gx, gy, rr + 30, hex(0x2f7f78), (0.10f + 0.12f * b) * a, R_ * 0.85f);
    s.ring(gx, gy, rr, 3, CALM, (0.25f + 0.4f * b) * a);
  }

  // dunkle Partikel vor dem Körper, damit man sieht, wie sie im Mund landen
  drawBody(s);
  for (int i = 0; i < nDark_; i++) {
    const Dark& d = dark_[i];
    const float a = d.homing ? 1.f : 0.75f;
    s.glow(d.x, d.y, d.r * 3.f, skin_.darkRim, 0.6f * a);
    s.circle(d.x, d.y, d.r + 1.2f, skin_.darkEdge, 0.8f * a);
    s.circle(d.x, d.y, d.r, skin_.darkCore, a);
    s.circle(d.x - d.r * 0.3f, d.y - d.r * 0.3f, d.r * 0.38f, skin_.kid ? hex(0xff9a70) : DARK_HI, 0.7f * a);
  }

  // helle Blasen, Funken, Blüten, „z“
  for (int i = 0; i < nLight_; i++) {
    const Light& l = light_[i];
    const float a = std::fmin(1.f, l.life / 0.3f) * (1 - smooth((l.life - l.max * 0.6f) / (l.max * 0.4f)));
    if (a <= 0.01f) continue;
    switch (l.kind) {
      case 0:   // Blase
        s.ring(l.x, l.y, l.r, 2, l.c, 0.8f * a);
        s.circle(l.x - l.r * 0.35f, l.y - l.r * 0.35f, l.r * 0.22f, hex(0xffffff), 0.7f * a);
        break;
      case 1:   // Funke
        s.glow(l.x, l.y, l.r * 3.2f, l.c, 0.7f * a);
        s.circle(l.x, l.y, l.r * 0.45f, mix(l.c, hex(0xffffff), 0.6f), a);
        break;
      case 2:   // Blüte
        for (int k = 0; k < 5; k++) {
          const float an = l.ph + k * TAU / 5 + l.life * 0.8f;
          s.circle(l.x + std::sin(an) * l.r * 0.6f, l.y - std::cos(an) * l.r * 0.6f, l.r * 0.45f, l.c, 0.85f * a);
        }
        s.circle(l.x, l.y, l.r * 0.32f, hex(0xffe58a), a);
        break;
      default:  // z
        s.text(fonts::small, "z", l.x, l.y, l.c, 0.7f * a, Align::Center);
        break;
    }
  }

  // „BÖRPS“
  if (burpT_ >= 0) {
    const float a = std::fmin(1.f, burpT_ / 0.15f) * (1 - smooth((burpT_ - 0.8f) / 0.8f));
    s.glow(gx + 30, gy - R_ * 0.2f - burpT_ * 30, 46 + burpT_ * 30, hex(0x5a6a5a), 0.35f * a);
    s.text(fonts::title, "BÖRPS", gx + 34, gy - R_ * 1.25f - 20 - burpT_ * 22, hex(0xd8f0b0), a, Align::Center);
  }

  // Texte unten
  const float ty = 376;
  char buf[64];
  if (phase_ == CALIB) {
    drawCalib(s);
  } else if (meter_) {
    // Messwerte: Ausschläge der letzten zwei Sekunden, Linie = Schwelle
    const float top = 356, hgt = 44, x0 = 70, bw = (W - 140) / 64.f;
    const float full = thr_ * 3;
    float mx = 0;
    for (int i = 0; i < 64; i++) {
      const float v = peaks_[(peakI_ + i) % 64];
      mx = std::fmax(mx, v);
      const float h = std::fmin(hgt, v / full * hgt);
      if (h > 0.5f) s.rrect(x0 + i * bw, top + hgt - h, std::fmax(1.5f, bw - 1), h, 1, v >= thr_ ? hex(0xff9a5a) : hex(0x6a6488));
    }
    s.capsule(x0 - 4, top + hgt * 2 / 3, W - x0 + 4, top + hgt * 2 / 3, 0.8f, hex(0xb7a6ff), 0.8f);
    snprintf(buf, sizeof buf, "Spitze %.2f g · Schwelle %.2f g", mx, thr_);
    for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
    s.text(fonts::small, buf, W / 2, top + hgt + 6, TXT_DIM, 1, Align::Center);
  } else if (phase_ == READY) {
    const float k = std::fmod(hintT_, 40.f);
    const float a = smooth((k - 1.5f) / 0.6f) * (1 - smooth((k - 7.f) / 0.8f));
    if (skin_.kid) {   // große Schrift, bei Bedarf zweizeilig
      char l[2][160];
      int n = wrapText(fonts::title, skin_.hint, 320, l, 2);
      if (n == 2) n = wrapText(fonts::title, skin_.hint, Scene::textW(fonts::title, skin_.hint) / 2 + 40, l, 2);   // ausgewogen
      if (n < 0) n = wrapText(fonts::title, skin_.hint, 320, l, 2);
      if (n < 0) n = 2;
      for (int i = 0; i < n; i++) s.text(fonts::title, l[i], W / 2, ty - (n - 1) * 20 + i * 38, TXT, a, Align::Center);
    } else {
      s.text(fonts::small, skin_.hint, W / 2, ty + 10, TXT_DIM, a, Align::Center);
    }
  } else if (phase_ == BREATHE) {
    if (t_ < INTRO) {
      const float a = smooth(t_ / 0.5f) * (1 - smooth((t_ - INTRO + 0.5f) / 0.5f));
      s.text(skin_.kid ? fonts::title : fonts::body, skin_.intro, W / 2, ty, TXT, a, Align::Center);
    } else {
      const float L = skin_.inhale + skin_.exhale, u = std::fmod(t_ - INTRO, L);
      const bool in = u < skin_.inhale;
      const float pu = in ? u : u - skin_.inhale, pl = in ? skin_.inhale : skin_.exhale;
      const float a = smooth(pu / 0.5f) * (1 - smooth((pu - pl + 0.5f) / 0.5f));
      s.text(skin_.kid ? fonts::title : fonts::body, in ? "Einatmen" : "Ausatmen", W / 2, ty - (skin_.kid ? 16 : 0), TXT, 0.9f * a, Align::Center);
      const char* sub = in ? skin_.inhaleHint : skin_.exhaleHint;
      if (sub) s.text(fonts::body, sub, W / 2, ty + 22, TXT_DIM, a, Align::Center);
      for (int i = 0; i < skin_.breaths; i++) {
        const float x = W / 2 + (i - (skin_.breaths - 1) * 0.5f) * 16;
        s.circle(x, ty + (skin_.kid ? 58 : 46), 3.5f, i <= breathN_ ? CALM : hex(0x3a3550), 0.8f);
      }
    }
  } else if (phase_ == THANKS) {
    const float a = smooth(t_ / 0.6f) * (1 - smooth((t_ - THANKS_DUR + 1.f) / 1.f));
    if (skin_.kid) {
      s.text(fonts::title, skin_.thanks, W / 2, ty - 6, TXT, a, Align::Center);
      drawStarCount(s, W / 2, ty + 38, stars_, a);
    } else {
      s.text(fonts::body, skin_.thanks, W / 2, ty, TXT, a, Align::Center);
      statsText(buf, sizeof buf);
      s.text(fonts::small, buf, W / 2, ty + 36, TXT_DIM, a, Align::Center);
    }
  }
}

// ---------------- Klänge ----------------

void Grummel::sMunch() {
  Synth::Voice a; a.wave = Synth::NOISE; a.filt = Synth::LP; a.cut0 = 520; a.cut1 = 240; a.dur = 0.09f; a.vol = 0.45f; synth_.play(a);
  Synth::Voice b; b.wave = Synth::SINE; b.f0 = 140 + rnd() * 50; b.f1 = 70; b.dur = 0.09f; b.vol = 0.35f; synth_.play(b);
}

void Grummel::sBurp() {
  Synth::Voice a; a.wave = Synth::SAW; a.filt = Synth::LP; a.cut0 = 750; a.cut1 = 320; a.q = 1.3f;
  a.f0 = 118; a.f1 = 68; a.lfoF = 23; a.lfoD = 16; a.attack = 0.03f; a.dur = 0.65f; a.vol = 0.55f; synth_.play(a);
  Synth::Voice b; b.wave = Synth::SQUARE; b.filt = Synth::LP; b.cut0 = b.cut1 = 380; b.f0 = 59; b.f1 = 44; b.attack = 0.03f; b.dur = 0.55f; b.vol = 0.22f; synth_.play(b);
  Synth::Voice c; c.wave = Synth::NOISE; c.filt = Synth::LP; c.cut0 = c.cut1 = 330; c.dur = 0.25f; c.vol = 0.2f; synth_.play(c);
}

void Grummel::sRise() {
  const float notes[6] = {523, 587, 659, 784, 880, 1047};   // Pentatonik aufwärts
  for (int i = 0; i < 6; i++) {
    Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = notes[i]; v.attack = 0.02f; v.dur = 0.55f; v.delay = 0.17f * i; v.vol = 0.11f; synth_.play(v);
    Synth::Voice h = v; h.wave = Synth::TRI; h.f0 = h.f1 = notes[i] * 2; h.vol = 0.025f; synth_.play(h);
  }
}

void Grummel::sHuh() {
  Synth::Voice v; v.wave = Synth::TRI; v.f0 = 320; v.f1 = 540; v.dur = 0.15f; v.vol = 0.22f; synth_.play(v);
}

}  // namespace dk
