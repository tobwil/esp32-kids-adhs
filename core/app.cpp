#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"

namespace dk {

static constexpr int BRIGHT[3] = {200, 120, 60}, BRIGHT_DIM = 40;
static constexpr uint32_t LOCKED_HOLD_MS = 8000;   // bei Sperre länger halten, bis die Einstellungen kommen
static constexpr float LIST_Y0 = 76, LIST_Y1 = H - 10, PILL_H = 50, ROW_GAP = 10, HEAD_H = 34, INFO_H = 26;
static constexpr float TRANS_OUT = 0.12f, TRANS_IN = 0.18f;   // s
static constexpr float TAP_MOVE = 20, TAP_MS = 700, EDGE = 36;    // Tipp: so wenig bewegt, so kurz; Rand für Wischen über Spiele
static constexpr float SWIPE_GO = 70, SWIPE_FLICK = 35, SWIPE_FLICK_MS = 300;

static const Col TXT = hex(0xe8e4f4), PILL = hex(0x2a2540), ACCENT = hex(0xb7a6ff);
static const Col TXT_DIM = hex(0x9a94b4);
static const char* const SOUND_NAMES[3] = {"aus", "leise", "an"};
static const char* const BRIGHT_NAMES[3] = {"hell", "mittel", "dunkel"};

App::App(Synth& synth, Store& store) : synth_(synth), store_(store) {}

void App::begin(uint32_t ms) {
  ms_ = lastAct_ = ms;
  if (startOff_) { lastAct_ = ms - offMs_ - 1; off_ = true; }   // schwarz bis zum ersten Tippen
  sound_ = store_.get("snd", 0);   // Ton startet aus
  brightLevel_ = store_.get("hell", 0) % 3;
  lockMode_ = store_.get("lk_mode", 0);
  lockFrom_ = store_.get("lk_from", lockFrom_);
  lockTo_ = store_.get("lk_to", lockTo_);
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
  slideX_ = slideY_ = dragX_ = dragY_ = 0;
  if (i == home_) for (int& b : back_) b = -1;
  setCurrent(i);
}

void App::setLock(int mode, int from, int to) {
  if (mode == lockMode_ && from == lockFrom_ && to == lockTo_) return;
  lockMode_ = mode; lockFrom_ = from; lockTo_ = to;
  store_.set("lk_mode", mode); store_.set("lk_from", from); store_.set("lk_to", to);
}

void App::updateLock(int64_t epoch) {
  bool on = lockTile_ >= 0 && lockMode_ == 1;
  if (lockTile_ >= 0 && lockMode_ == 2 && epoch > 0) {
    const int m = int((epoch / 60) % 1440);
    on = lockFrom_ < lockTo_ ? (m >= lockFrom_ && m < lockTo_) : (m >= lockFrom_ || m < lockTo_);
  }
  if (on == locked_) return;
  locked_ = on;
  if (on) { if (cur_ != lockTile_) go(lockTile_); }
  else if (cur_ == lockTile_) go(Tile::GOTO_HOME);
}

void App::go(int target) {
  bool returning = false;
  if (locked_) {   // gesperrt: nur zur erlaubten Kachel
    if (target != lockTile_ || cur_ == lockTile_) return;
    for (int& b : back_) b = -1;
    transTo_ = target; transReturning_ = false; trans_ = 0.0001f;
    return;
  }
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
  const int start = locked_ ? lockTile_ : home_;
  if (off_ && cur_ != start) show(start);
  lastAct_ = ms_;
}

int App::swipeTarget(float d) const {
  // nach oben gewischt (d < 0): nächste App; nach unten: vorige, vor der ersten liegt das Dashboard
  const int n = int(tiles_.size());
  if (d < 0) {
    int t = cur_ + 1;
    if (t == home_) t++;
    return t < n ? t : -1;
  }
  if (cur_ == home_) return -1;
  const int t = cur_ - 1;
  return t < 0 ? home_ : t;
}

int App::settingsRows(Row* out, int cap) const {
  int n = 0;
  auto add = [&](RowKind k, int i) { if (n < cap) out[n++] = {k, i}; };
  const int nt = tiles_[cur_]->settingsCount();
  if (nt) { add(R_HEAD, 0); for (int i = 0; i < nt; i++) add(R_TILE, i); }
  add(R_HEAD, 1);
  add(R_SOUND, 0);
  add(R_BRIGHT, 0);
  if (lockTile_ >= 0) add(R_LOCK, 0);
  if (sys_ && sys_->count()) { add(R_HEAD, 2); for (int i = 0; i < sys_->count(); i++) add(R_SYS, i); }
  add(R_DONE, 0);
  return n;
}

float App::rowH(const Row& r) const {
  if (r.kind == R_HEAD) return HEAD_H;
  if (!rowPill(r)) return INFO_H;
  return PILL_H + ROW_GAP;
}

void App::rowText(const Row& r, char* buf, int n) const {
  switch (r.kind) {
    case R_HEAD: snprintf(buf, n, "%s", r.idx == 0 ? tiles_[cur_]->title() : r.idx == 1 ? "Allgemein" : "Verbindung"); break;
    case R_TILE: tiles_[cur_]->settingText(r.idx, buf, n); break;
    case R_SOUND: snprintf(buf, n, "Ton: %s", SOUND_NAMES[sound_]); break;
    case R_BRIGHT: snprintf(buf, n, "Helligkeit: %s", BRIGHT_NAMES[brightLevel_]); break;
    case R_LOCK:
      if (lockMode_ == 2) snprintf(buf, n, "Sperre: %d:%02d–%d:%02d", lockFrom_ / 60, lockFrom_ % 60, lockTo_ / 60, lockTo_ % 60);
      else snprintf(buf, n, "Sperre: %s", lockMode_ ? "nur Radio" : "aus");
      break;
    case R_SYS: sys_->text(r.idx, buf, n); break;
    case R_DONE: snprintf(buf, n, "Fertig"); break;
  }
}

float App::settingsMaxScroll() const {
  Row rows[40];
  const int n = settingsRows(rows, 40);
  float h = 0;
  for (int i = 0; i < n; i++) h += rowH(rows[i]);
  return std::fmax(0.f, h - (LIST_Y1 - LIST_Y0));
}

void App::openSettings() {
  settings_ = true;
  scroll_ = 0;
  wake();
}

void App::settingsTap(float x, float y) {
  if (y < LIST_Y0 - 6) { settings_ = false; return; }   // auf die Überschrift getippt: schließen
  Row rows[40];
  const int n = settingsRows(rows, 40);
  float ry = LIST_Y0 - scroll_;
  for (int i = 0; i < n; i++) {
    const Row& r = rows[i];
    const float h = rowH(r);
    if (rowPill(r) && y >= ry && y < ry + PILL_H && x > 30 && x < W - 30) {
      switch (r.kind) {
        case R_SOUND:
          sound_ = (sound_ + 1) % 3;
          store_.set("snd", sound_);
          applySound();
          if (sound_) { Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = 880; v.dur = 0.12f; v.vol = 0.25f; synth_.play(v); }
          break;
        case R_BRIGHT: brightLevel_ = (brightLevel_ + 1) % 3; store_.set("hell", brightLevel_); break;
        case R_LOCK: setLock((lockMode_ + 1) % 3, lockFrom_, lockTo_); lockDirty_ = true; break;   // aus → nur Radio → Zeitplan
        case R_TILE: if (tiles_[cur_]->settingTap(r.idx)) settings_ = false; break;
        case R_SYS: sys_->tap(r.idx); break;
        case R_DONE: settings_ = false; break;
        default: break;
      }
      return;
    }
    ry += h;
  }
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
  updateLock(in.epoch);
  ti.locked = locked_;

  // ---- Touch ----
  // beim Schütteln liegen Finger am Rand: kein Tippen oder langer Druck bis kurz danach
  if (motion.shaking) shakeMs_ = in.ms;
  const bool shakeLock = shakeMs_ && in.ms - shakeMs_ < 600;
  if (shakeLock && mode_ != NONE) mode_ = DONE;
  const bool wasOff = off_;   // gedimmt reagiert alles normal, nur aus dem Schwarzen weckt die erste Berührung bloß auf
  for (int i = 0; i < in.nTouch; i++) {
    const TouchSample& t = in.touch[i];
    if (t.down && !touching_) {
      touching_ = true;
      sx_ = lx_ = t.x; sy_ = ly_ = t.y; sMs_ = t.ms;
      touchPeak_ = 0;
      mode_ = off_ || shakeLock ? DONE : PENDING;
      const bool edge = t.x < EDGE || t.x > W - EDGE || t.y < EDGE || t.y > H - EDGE;
      swipeOk_ = !settings_ && !locked_ && trans_ <= 0 && (edge || !tiles_[cur_]->claimsDragAt(t.x, t.y));
      scrollStart_ = scroll_;
      wake();
    } else if (t.down && touching_) {
      lx_ = t.x; ly_ = t.y;
      const float dx = lx_ - sx_, dy = ly_ - sy_;
      if (mode_ == PENDING && dx * dx + dy * dy > TAP_MOVE * TAP_MOVE) {   // gezogen statt getippt
        mode_ = DONE;
        if (swipeOk_) {
          swipeH_ = std::fabs(dx) > std::fabs(dy);
          if (!swipeH_ || cur_ != home_) mode_ = SWIPE;   // auf dem Dashboard schließt waagerecht nichts
        }
      }
      if (settings_ && mode_ == DONE) scroll_ = std::fmax(0.f, std::fmin(settingsMaxScroll(), scrollStart_ - dy));   // Liste verschieben
      if (mode_ == SWIPE) {
        // Bild folgt dem Finger; geht es in die Richtung nicht weiter, nur ein wenig (Gummiband)
        const float d = swipeH_ ? dx : dy;
        const bool can = swipeH_ || swipeTarget(d) >= 0;
        const float v = can ? d * 0.6f : d * 0.15f;
        dragX_ = swipeH_ ? v : 0;
        dragY_ = swipeH_ ? 0 : v;
      }
      wake();
    } else if (!t.down && touching_) {
      touching_ = false;
      releaseMs_ = t.ms;
      const float dx = t.x - sx_, dy = t.y - sy_;
      if (mode_ == PENDING && t.ms - sMs_ < TAP_MS && dx * dx + dy * dy < TAP_MOVE * TAP_MOVE) {
        if (settings_) settingsTap(sx_, sy_);
        else { ti.tap = true; ti.tapX = sx_; ti.tapY = sy_; ti.tapPeak = touchPeak_; }
      } else if (mode_ == SWIPE && transTo_ < 0) {
        const float d = swipeH_ ? dx : dy;
        const bool far = std::fabs(d) > SWIPE_GO || (std::fabs(d) > SWIPE_FLICK && t.ms - sMs_ < SWIPE_FLICK_MS);
        if (far) {
          const float dir = d < 0 ? -1.f : 1.f;
          if (swipeH_) { go(Tile::GOTO_HOME); slideX_ = dir; }   // schließen
          else if (swipeTarget(d) >= 0) {
            const int to = swipeTarget(d);
            go(to);
            if (to != home_) back_[to] = -1;   // geblättert, nicht geöffnet: zurück geht es aufs Dashboard
            slideY_ = dir;
          }
        }
      }
      mode_ = NONE;
    }
  }
  if (mode_ != SWIPE && trans_ <= 0) {   // zurückfedern
    dragX_ += -dragX_ * std::fmin(1.f, dt_ * 14);
    dragY_ += -dragY_ * std::fmin(1.f, dt_ * 14);
  }
  // langer Druck öffnet die Einstellungen (Spiele mit Finger-Steuerung: vom Rand aus halten)
  if (touching_ && mode_ == PENDING && !settings_ && in.ms - sMs_ > (locked_ ? LOCKED_HOLD_MS : holdMs_) &&
      (!tiles_[cur_]->claimsDrag() || sx_ < EDGE || sx_ > W - EDGE)) {
    openSettings();
    mode_ = DONE;
  }
  ti.finger = touching_ && !settings_ && !wasOff && mode_ != SWIPE;
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
      if (trans_ <= 0) { trans_ = 0; slideX_ = slideY_ = 0; dragX_ = dragY_ = 0; }
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
  bright_ = off_ ? 0 : dim_ ? std::min(BRIGHT_DIM, BRIGHT[brightLevel_]) : BRIGHT[brightLevel_];
  // alle 60 s wandert das Bild um ein paar Pixel auf einer kleinen Kreisbahn
  const float ph = float((in.ms / 60000) % 8) * (TAU / 8);
  orbX_ += (3.f * std::cos(ph) - orbX_) * std::fmin(1.f, dt_ * 0.5f);
  orbY_ += (3.f * std::sin(ph) - orbY_) * std::fmin(1.f, dt_ * 0.5f);
  settingsA_ += ((settings_ ? 1.f : 0.f) - settingsA_) * std::fmin(1.f, dt_ * 12);
}

void App::draw(Scene& s) {
  s.reset();
  if (off_) return;
  // nach einem Wisch: altes Bild gleitet weiter hinaus, das neue kommt von der anderen Seite
  float slide = 0;
  if (slideX_ != 0 || slideY_ != 0) slide = transTo_ >= 0 ? trans_ * 120 : -trans_ * 60;
  s.ox = std::round(orbX_ + dragX_ * (transTo_ >= 0 || trans_ <= 0 ? 1.f : 0.f) + slideX_ * slide);
  s.oy = std::round(orbY_ + dragY_ * (transTo_ >= 0 || trans_ <= 0 ? 1.f : 0.f) + slideY_ * slide);
  const float tf = 1 - std::fmin(1.f, trans_);
  s.fade = tf * (1.f - settingsA_ * 0.9f);
  tiles_[cur_]->draw(s);
  s.fade = 1;
  if (settingsA_ > 0.02f) drawSettings(s);
}

void App::drawSettings(Scene& s) {
  const float a = settingsA_;
  s.text(fonts::title, "Einstellungen", W / 2, 22, TXT, a, Align::Center);
  Row rows[40];
  const int n = settingsRows(rows, 40);
  char buf[64];
  float y = LIST_Y0 - scroll_;
  for (int i = 0; i < n; i++) {
    const Row& r = rows[i];
    const float h = rowH(r);
    const float vis = rowPill(r) ? PILL_H : h;
    if (y >= LIST_Y0 - 4 && y + vis <= LIST_Y1) {   // nur ganz sichtbare Zeilen
      rowText(r, buf, sizeof buf);
      if (r.kind == R_HEAD) {
        s.text(fonts::small, buf, 52, y + 6, ACCENT, a);
      } else if (!rowPill(r)) {
        s.text(fonts::small, buf, W / 2, y, TXT_DIM, a, Align::Center);
      } else {
        const bool last = r.kind == R_DONE;
        s.rrect(40, y, W - 80, PILL_H, PILL_H / 2, last ? scale(ACCENT, 0.35f) : PILL, a);
        s.text(fonts::body, buf, W / 2, y + (PILL_H - fonts::body.lineH) / 2 + 1, last ? ACCENT : TXT, a, Align::Center);
      }
    }
    y += h;
  }
  // Rollbalken, wenn die Liste länger ist als der Bildschirm
  const float maxS = settingsMaxScroll();
  if (maxS > 0) {
    const float vh = LIST_Y1 - LIST_Y0, th = vh * vh / (vh + maxS), ty = LIST_Y0 + (vh - th) * scroll_ / maxS;
    s.capsule(W - 20, ty + 3, W - 20, ty + th - 3, 2.5f, TXT_DIM, 0.6f * a);
  }
}

const char* App::debug() {
  snprintf(dbg_, sizeof dbg_, "kachel %s  hell %d  klopfen %lu  spitze %.3f g  ruhe %.3f g  schwelle %.2f", tiles_[cur_]->title(), bright_,
           (unsigned long)motion.totalKnocks, motion.framePeak, motion.noise, motion.threshold);
  return dbg_;
}

}  // namespace dk
