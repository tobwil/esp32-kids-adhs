#include "app.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"

namespace dk {

static constexpr int BRIGHT = 200, BRIGHT_DIM = 40;
static constexpr float SWIPE_MIN = 70;          // px, ab hier wechselt die Kachel
static constexpr float SETTINGS_Y0 = 100, SETTINGS_H = 52, SETTINGS_GAP = 12;

static const Col TXT = hex(0xe8e4f4), TXT_DIM = hex(0x8c86a6), PILL = hex(0x2a2540), ACCENT = hex(0xb7a6ff);
static const char* const SOUND_NAMES[3] = {"aus", "leise", "an"};

App::App(Synth& synth, Store& store) : synth_(synth), store_(store) {}

void App::begin(uint32_t ms) {
  ms_ = lastAct_ = ms;
  if (startOff_) { lastAct_ = ms - offMs_ - 1; off_ = true; }   // schwarz bis zum ersten Tippen
  sound_ = store_.get("snd", 0);   // Ton startet aus
  applySound();
  int t = home_ >= 0 ? home_ : store_.get("tile", int(tiles_.size()) - 1);
  cur_ = wrap(t);
  pos_ = target_ = float(cur_);
  tiles_[cur_]->enter(ms);
  titleT_ = 1.6f;
}

void App::applySound() { synth_.setVolume(sound_ == 0 ? 0.f : sound_ == 1 ? 0.3f : 0.8f); }

void App::setCurrent(int i) {
  i = wrap(i);
  if (i == cur_) return;
  tiles_[cur_]->leave();
  cur_ = i;
  tiles_[cur_]->enter(ms_);
  store_.set("tile", cur_);
  titleT_ = 1.6f;
}

void App::wake() {
  // nach Dimmen oder Abschalten immer zur Startkachel (vorhersehbarer Einstieg)
  if ((off_ || dim_) && home_ >= 0 && cur_ != home_) show(home_);
  lastAct_ = ms_;
}

// zu einer Kachel gleiten (auf dem kürzesten Weg im Kreis)
void App::slideTo(int i) {
  const int n = int(tiles_.size());
  i = wrap(i);
  if (i == cur_ && !sliding_) return;
  int d = i - cur_;
  if (d > n / 2) d -= n;
  if (d < -n / 2) d += n;
  target_ = float(cur_ + d);
  sliding_ = true;
  dotsT_ = 1.5f;
  arriveAt_ = i;
}

int App::settingsItems() const { return 1 + tiles_[cur_]->settingsCount() + 1; }   // Ton, Kachel…, Fertig

void App::openSettings() {
  settings_ = true;
  wake();
}

void App::settingsTap(float x, float y) {
  const int n = settingsItems();
  for (int i = 0; i < n; i++) {
    const float iy = SETTINGS_Y0 + i * (SETTINGS_H + SETTINGS_GAP);
    if (y >= iy && y < iy + SETTINGS_H && x > 30 && x < W - 30) {
      if (i == 0) {
        sound_ = (sound_ + 1) % 3;
        store_.set("snd", sound_);
        applySound();
        if (sound_) { Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = 880; v.dur = 0.12f; v.vol = 0.25f; synth_.play(v); }
      } else if (i == n - 1) {
        settings_ = false;
      } else {
        if (tiles_[cur_]->settingTap(i - 1)) settings_ = false;
      }
      return;
    }
  }
  settings_ = false;   // daneben getippt: schließen
}

void App::tick(const Input& in) {
  dt_ = ms_ ? (in.ms - ms_) * 0.001f : 0.033f;
  if (dt_ < 0 || dt_ > 0.2f) dt_ = 0.033f;
  ms_ = in.ms;

  TileInput ti;
  ti.ms = in.ms;
  ti.dt = dt_;
  ti.epoch = in.epoch;

  // ---- Touch-Gesten ----
  // beim Schütteln liegen Finger am Rand: kein Wischen, Tippen oder langer Druck bis kurz danach
  if (motion.shaking) shakeMs_ = in.ms;
  const bool shakeLock = shakeMs_ && in.ms - shakeMs_ < 600;
  if (shakeLock && mode_ != NONE) mode_ = DONE;
  for (int i = 0; i < in.nTouch; i++) {
    const TouchSample& t = in.touch[i];
    if (t.down && !touching_) {
      touching_ = true;
      sx_ = lx_ = t.x; sy_ = ly_ = t.y; sMs_ = lMs_ = t.ms; vx_ = 0;
      touchPeak_ = 0;
      mode_ = off_ || dim_ || shakeLock ? DONE : PENDING;   // erste Berührung weckt nur auf
      wake();
    } else if (t.down && touching_) {
      if (t.ms != lMs_) vx_ = vx_ * 0.5f + 0.5f * (t.x - lx_) / float(t.ms - lMs_);
      lx_ = t.x; ly_ = t.y; lMs_ = t.ms;
      const float dx = lx_ - sx_, dy = ly_ - sy_;
      const bool edge = sx_ < 30 || sx_ > W - 30;
      if (mode_ == PENDING && !settings_ && (edge || !tiles_[cur_]->claimsDrag())) {
        if (std::fabs(dx) > 16 && std::fabs(dx) > std::fabs(dy)) { mode_ = HDRAG; dragStartPos_ = pos_; sliding_ = false; }
        else if (dx * dx + dy * dy > 18 * 18) mode_ = DONE;
      }
      if (mode_ == HDRAG) { pos_ = dragStartPos_ - dx / W; dotsT_ = 1.5f; }
      wake();
    } else if (!t.down && touching_) {
      touching_ = false;
      releaseMs_ = t.ms;
      const float dx = t.x - sx_, dy = t.y - sy_;
      if (mode_ == PENDING && t.ms - sMs_ < 450 && dx * dx + dy * dy < 18 * 18) {
        if (settings_) settingsTap(sx_, sy_);
        else { ti.tap = true; ti.tapX = sx_; ti.tapY = sy_; ti.tapPeak = touchPeak_; }
      } else if (mode_ == HDRAG) {
        float tgt = std::round(dragStartPos_);
        if (dx < -SWIPE_MIN || vx_ < -0.6f) tgt += 1;
        else if (dx > SWIPE_MIN || vx_ > 0.6f) tgt -= 1;
        target_ = tgt;
        sliding_ = true;
      }
      mode_ = NONE;
    }
  }
  // langer Druck öffnet die Einstellungen
  if (touching_ && mode_ == PENDING && !settings_ && in.ms - sMs_ > holdMs_ && !tiles_[cur_]->claimsDrag()) { openSettings(); mode_ = DONE; }
  ti.finger = touching_ && mode_ != HDRAG && !settings_;
  ti.fingerX = lx_;
  ti.fingerY = ly_;
  // BOOT-Taste: Einstellungen auf/zu
  if (in.boot && !bootWas_) { if (off_ || dim_) wake(); else if (settings_) settings_ = false; else openSettings(); }
  bootWas_ = in.boot;

  // ---- Kachel einrasten ----
  if (sliding_) {
    pos_ += (target_ - pos_) * std::fmin(1.f, dt_ * 14.f);
    if (std::fabs(target_ - pos_) < 0.003f) {
      sliding_ = false;
      const int idx = wrap(int(std::lround(target_)));
      pos_ = target_ = float(idx);   // Position im Bereich 0..n-1 halten
      setCurrent(idx);
      if (arriveAt_ == idx) tiles_[idx]->arrive();
      arriveAt_ = -1;
    }
  }

  // ---- Bewegung ----
  Tile* tile = tiles_[cur_];
  motion.threshold = tile->motionThreshold();
  const bool touchBusy = touching_ || in.ms - releaseMs_ < 250;
  motion.feed(in.acc, in.nAcc, touchBusy);
  if (touching_ || in.ms - releaseMs_ < 60) touchPeak_ = std::fmax(touchPeak_, motion.framePeak);
  if (ti.tap) ti.tapPeak = std::fmax(ti.tapPeak, motion.framePeak);
  if (motion.nKnocks || motion.shaking) {
    if (off_ || dim_) { if (wakeOnMotion_) wake(); }
    else if (tile->wantsMotion()) {
      wake();
      ti.knocks = motion.knocks;
      ti.nKnocks = settings_ || sliding_ || mode_ == HDRAG ? 0 : motion.nKnocks;
      ti.shaking = motion.shaking;
      ti.shake = motion.shake;
    }
  }
  ti.framePeak = motion.framePeak;
  ti.tiltX = motion.tiltX; ti.tiltY = motion.tiltY; ti.tiltZ = motion.tiltZ;
  ti.threshold = std::fmax(motion.threshold, motion.noise * 4.f);

  if (off_ || dim_) { ti.tap = false; ti.nKnocks = 0; }   // die weckende Berührung zählt nicht
  if (!settings_) tile->tick(ti);
  const int go = tile->takeGoto();
  if (go >= 0 && !sliding_) slideTo(go);
  if (tile->keepAwake() || settings_) wake();

  // ---- Dimmen, Abschalten, Einbrennschutz ----
  const uint32_t idle = in.ms - lastAct_;
  off_ = idle > offMs_;
  dim_ = !off_ && idle > dimMs_;
  bright_ = off_ ? 0 : dim_ ? BRIGHT_DIM : BRIGHT;
  // alle 60 s wandert das Bild um ein paar Pixel auf einer kleinen Kreisbahn
  const float ph = float((in.ms / 60000) % 8) * (TAU / 8);
  orbX_ += (3.f * std::cos(ph) - orbX_) * std::fmin(1.f, dt_ * 0.5f);
  orbY_ += (3.f * std::sin(ph) - orbY_) * std::fmin(1.f, dt_ * 0.5f);

  dotsT_ = std::fmax(0, dotsT_ - dt_);
  titleT_ = std::fmax(0, titleT_ - dt_);
  settingsA_ += ((settings_ ? 1.f : 0.f) - settingsA_) * std::fmin(1.f, dt_ * 12);
}

void App::draw(Scene& s) {
  s.reset();
  if (off_) return;
  const int n = int(tiles_.size());
  const float base = std::floor(pos_);
  const float frac = pos_ - base;
  for (int k = 0; k < 2; k++) {
    if (k == 1 && frac < 0.001f) break;
    const int idx = wrap(int(base) + k);
    s.ox = std::round((k - frac) * W + orbX_);
    s.oy = std::round(orbY_);
    s.fade = 1.f - settingsA_ * 0.9f;
    tiles_[idx]->draw(s);
  }
  s.ox = std::round(orbX_);
  s.oy = std::round(orbY_);
  s.fade = 1;

  // Kachelname kurz nach dem Wechsel
  if (titleT_ > 0 && !settings_ && showTitles_) {
    const float a = std::fmin(1.f, titleT_ / 0.5f) * 0.85f;
    s.text(fonts::title, tiles_[cur_]->title(), W / 2, 22, TXT, a, Align::Center);
  }
  // Seitenpunkte beim Wischen
  if (dotsT_ > 0) {
    const float a = std::fmin(1.f, dotsT_ / 0.5f);
    for (int i = 0; i < n; i++) {
      const float x = W / 2 + (i - (n - 1) * 0.5f) * 18;
      float d = std::fabs(float(i) - (pos_ - n * std::floor(pos_ / n)));
      if (d > n / 2.f) d = n - d;
      const float on = std::fmax(0, 1 - d);
      s.circle(x, H - 22, 3.5f + 1.5f * on, mix(TXT_DIM, TXT, on), a * (0.5f + 0.5f * on));
    }
  }
  if (settingsA_ > 0.02f) drawSettings(s);
}

void App::drawSettings(Scene& s) {
  const float a = settingsA_;
  s.text(fonts::title, "Einstellungen", W / 2, 46, TXT, a, Align::Center);
  const int n = settingsItems();
  char buf[48];
  for (int i = 0; i < n; i++) {
    const float y = SETTINGS_Y0 + i * (SETTINGS_H + SETTINGS_GAP);
    const bool last = i == n - 1;
    if (i == 0) snprintf(buf, sizeof buf, "Ton: %s", SOUND_NAMES[sound_]);
    else if (last) snprintf(buf, sizeof buf, "Fertig");
    else tiles_[cur_]->settingText(i - 1, buf, sizeof buf);
    s.rrect(40, y, W - 80, SETTINGS_H, SETTINGS_H / 2, last ? scale(ACCENT, 0.35f) : PILL, a);
    s.text(fonts::body, buf, W / 2, y + (SETTINGS_H - fonts::body.lineH) / 2 + 1, last ? ACCENT : TXT, a, Align::Center);
  }
}

const char* App::debug() {
  snprintf(dbg_, sizeof dbg_, "kachel %s  hell %d  klopfen %lu  spitze %.3f g  ruhe %.3f g  schwelle %.2f", tiles_[cur_]->title(), bright_,
           (unsigned long)motion.totalKnocks, motion.framePeak, motion.noise, motion.threshold);
  return dbg_;
}

}  // namespace dk
