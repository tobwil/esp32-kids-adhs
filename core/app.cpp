#include "app.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"

namespace dk {

static constexpr int BRIGHT = 200, BRIGHT_DIM = 40;
static constexpr float SETTINGS_Y0 = 100, SETTINGS_H = 52, SETTINGS_GAP = 12;
static constexpr float TRANS_OUT = 0.12f, TRANS_IN = 0.18f;   // s

static const Col TXT = hex(0xe8e4f4), PILL = hex(0x2a2540), ACCENT = hex(0xb7a6ff);
static const char* const SOUND_NAMES[3] = {"aus", "leise", "an"};

App::App(Synth& synth, Store& store) : synth_(synth), store_(store) {}

void App::begin(uint32_t ms) {
  ms_ = lastAct_ = ms;
  if (startOff_) { lastAct_ = ms - offMs_ - 1; off_ = true; }   // schwarz bis zum ersten Tippen
  sound_ = store_.get("snd", 0);   // Ton startet aus
  applySound();
  cur_ = home_;
  tiles_[cur_]->enter(ms);
}

void App::applySound() { synth_.setVolume(sound_ == 0 ? 0.f : sound_ == 1 ? 0.3f : 0.8f); }

void App::setCurrent(int i) {
  if (i < 0 || i >= int(tiles_.size()) || i == cur_) return;
  tiles_[cur_]->leave();
  cur_ = i;
  tiles_[cur_]->enter(ms_);
}

void App::show(int i) {
  if (i < 0 || i >= int(tiles_.size())) return;
  transTo_ = -1; trans_ = 0;
  if (i == home_) for (int& b : back_) b = -1;
  setCurrent(i);
}

void App::go(int target) {
  bool returning = false;
  if (target == Tile::GOTO_HOME) target = home_;
  else if (target == Tile::GOTO_BACK) {
    target = back_[cur_] >= 0 ? back_[cur_] : home_;
    returning = true;
  } else if (target >= 0 && target < int(tiles_.size()) && target != home_) {
    back_[target] = cur_;   // merken, wer die App geöffnet hat
  }
  if (target < 0 || target >= int(tiles_.size()) || target == cur_) return;
  if (target == home_) for (int& b : back_) b = -1;
  transTo_ = target;
  transReturning_ = returning;
  trans_ = 0.0001f;
}

void App::wake() {
  // nach Abschalten immer aufs Dashboard (vorhersehbarer Einstieg)
  if (off_ && cur_ != home_) show(home_);
  lastAct_ = ms_;
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
  ti.voice = in.voice;

  // ---- Touch ----
  // beim Schütteln liegen Finger am Rand: kein Tippen oder langer Druck bis kurz danach
  if (motion.shaking) shakeMs_ = in.ms;
  const bool shakeLock = shakeMs_ && in.ms - shakeMs_ < 600;
  if (shakeLock && mode_ != NONE) mode_ = DONE;
  const bool wasOff = off_ || dim_;
  for (int i = 0; i < in.nTouch; i++) {
    const TouchSample& t = in.touch[i];
    if (t.down && !touching_) {
      touching_ = true;
      sx_ = lx_ = t.x; sy_ = ly_ = t.y; sMs_ = t.ms;
      touchPeak_ = 0;
      mode_ = off_ || dim_ || shakeLock ? DONE : PENDING;   // erste Berührung weckt nur auf
      wake();
    } else if (t.down && touching_) {
      lx_ = t.x; ly_ = t.y;
      const float dx = lx_ - sx_, dy = ly_ - sy_;
      if (mode_ == PENDING && dx * dx + dy * dy > 18 * 18) mode_ = DONE;   // gezogen statt getippt
      wake();
    } else if (!t.down && touching_) {
      touching_ = false;
      releaseMs_ = t.ms;
      const float dx = t.x - sx_, dy = t.y - sy_;
      if (mode_ == PENDING && t.ms - sMs_ < 450 && dx * dx + dy * dy < 18 * 18) {
        if (settings_) settingsTap(sx_, sy_);
        else if (cur_ != home_ && tiles_[cur_]->showsClose() &&
                 (sx_ - CLOSE_X) * (sx_ - CLOSE_X) + (sy_ - CLOSE_Y) * (sy_ - CLOSE_Y) < (CLOSE_R + 12) * (CLOSE_R + 12))
          go(Tile::GOTO_HOME);   // ✕
        else { ti.tap = true; ti.tapX = sx_; ti.tapY = sy_; ti.tapPeak = touchPeak_; }
      }
      mode_ = NONE;
    }
  }
  // langer Druck öffnet die Einstellungen
  if (touching_ && mode_ == PENDING && !settings_ && in.ms - sMs_ > holdMs_ && !tiles_[cur_]->claimsDrag()) { openSettings(); mode_ = DONE; }
  ti.finger = touching_ && !settings_ && !wasOff;
  ti.fingerX = lx_;
  ti.fingerY = ly_;
  // BOOT-Taste: Einstellungen auf/zu
  if (in.boot && !bootWas_) { if (off_ || dim_) wake(); else if (settings_) settings_ = false; else openSettings(); }
  bootWas_ = in.boot;

  // ---- Überblendung ----
  if (trans_ > 0) {
    if (transTo_ >= 0) {
      trans_ += dt_ / TRANS_OUT;
      if (trans_ >= 1) {
        const int to = transTo_;
        transTo_ = -1;
        setCurrent(to);
        tiles_[to]->arrive(transReturning_);
        trans_ = 1;
      }
    } else {
      trans_ -= dt_ / TRANS_IN;
      if (trans_ <= 0) trans_ = 0;
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
      ti.nKnocks = settings_ || trans_ > 0 ? 0 : motion.nKnocks;
      ti.shaking = motion.shaking;
      ti.shake = motion.shake;
    }
  }
  ti.framePeak = motion.framePeak;
  ti.tiltX = motion.tiltX; ti.tiltY = motion.tiltY; ti.tiltZ = motion.tiltZ;
  ti.threshold = std::fmax(motion.threshold, motion.noise * 4.f);

  if (wasOff || trans_ > 0) { ti.tap = false; ti.nKnocks = 0; ti.finger = false; }   // weckende Berührung, Überblendung
  if (!settings_) tile->tick(ti);
  const int g = tile->takeGoto();
  if (g != -1 && transTo_ < 0) go(g);
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
  settingsA_ += ((settings_ ? 1.f : 0.f) - settingsA_) * std::fmin(1.f, dt_ * 12);
}

void App::draw(Scene& s) {
  s.reset();
  if (off_) return;
  s.ox = std::round(orbX_);
  s.oy = std::round(orbY_);
  const float tf = 1 - std::fmin(1.f, trans_);
  s.fade = tf * (1.f - settingsA_ * 0.9f);
  tiles_[cur_]->draw(s);
  // ✕ zum Schließen der App
  if (cur_ != home_ && tiles_[cur_]->showsClose()) {
    s.circle(CLOSE_X, CLOSE_Y, CLOSE_R, hex(0x2c2a48), 0.9f);
    const float k = 6.5f;
    s.capsule(CLOSE_X - k, CLOSE_Y - k, CLOSE_X + k, CLOSE_Y + k, 2.2f, hex(0xd8d4ec));
    s.capsule(CLOSE_X - k, CLOSE_Y + k, CLOSE_X + k, CLOSE_Y - k, 2.2f, hex(0xd8d4ec));
  }
  s.fade = 1;
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
