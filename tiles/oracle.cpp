#include "oracle.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"

namespace dk {

static const Col TXT = hex(0xe8e4f4), TXT_DIM = hex(0x8c86a6), PILL = hex(0x2a2540);
static const Col GOOD = hex(0x2e8b62), BAD = hex(0x9a3d55), GOOD_TXT = hex(0x8ff0c8), BAD_TXT = hex(0xffb3c6);
static const char* const DIFF_NAMES[3] = {"leicht", "mittel", "fies"};

static constexpr float OX = W / 2, OY = 210, OR = 72;                 // Kugel
static constexpr float PX = 24, PW = W - 48, PH = 58, PY0 = 222, PGAP = 68;   // Antwortkacheln
static constexpr float IDLE_SLEEP = 90;                                // s ohne Bedienung, dann schläft das Orakel wieder

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static int weekOf(int64_t epoch) { return int((epoch / 86400 + 3) / 7); }

Oracle::Oracle(Synth& synth, Store& store, uint32_t seed) : synth_(synth), store_(store), seed_(seed | 1) {
  cat_ = store_.get("or_cat", -1);
  diff_ = store_.get("or_diff", -1);
  nudge_ = store_.get("or_nudge", 0) != 0;
  streak_ = store_.get("or_ser", 0);
  best_ = store_.get("or_best", 0);
  wkId_ = store_.get("or_wk", -1);
  wkRight_ = store_.get("or_wr", 0);
  wkAll_ = store_.get("or_wn", 0);
  allRight_ = store_.get("or_allr", 0);
  all_ = store_.get("or_all", 0);
}

float Oracle::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return (seed_ & 0xFFFFFF) / float(0x1000000);
}

// Reihenfolge: gemischt mit gespeichertem Startwert, damit nach einem Neustart keine Frage doppelt kommt
void Oracle::shuffleOrder(uint32_t seed) {
  const int n = pack_->count();
  order_.resize(n);
  for (int i = 0; i < n; i++) order_[i] = uint16_t(i);
  uint32_t x = seed | 1;
  for (int i = n - 1; i > 0; i--) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    const int j = int(x % uint32_t(i + 1));
    const uint16_t t = order_[i]; order_[i] = order_[j]; order_[j] = t;
  }
}

void Oracle::setPack(QuizPack* pack) {
  pack_ = pack && pack->count() ? pack : nullptr;
  order_.clear();
  if (!pack_) return;
  const int h = int(pack_->hash() & 0x7FFFFFFF);
  if (store_.get("or_pack", 0) != h) {   // neues Paket: frisch mischen
    store_.set("or_pack", h);
    store_.set("or_seed", int(seed_ & 0x7FFFFFFF));
    store_.set("or_pos", 0);
  }
  shuffleOrder(uint32_t(store_.get("or_seed", 1)));
  pos_ = store_.get("or_pos", 0) % pack_->count();
  if (cat_ >= pack_->categories()) cat_ = -1;
}

bool Oracle::matches(int i) const {
  return (cat_ < 0 || pack_->categoryOf(i) == cat_) && (diff_ < 0 || pack_->diffOf(i) == diff_);
}

void Oracle::nextQuestion() {
  const int n = pack_->count();
  int pick = -1;
  for (int k = 0; k < n && pick < 0; k++) {
    const int i = order_[pos_];
    pos_++;
    if (pos_ >= n) {   // einmal durch: neu mischen
      pos_ = 0;
      const int s = int(seed_ & 0x7FFFFFFF);
      store_.set("or_seed", s);
      shuffleOrder(uint32_t(s));
    }
    if (matches(i)) pick = i;
  }
  if (pick < 0) pick = order_[pos_];   // Filter passt auf nichts: irgendeine
  store_.set("or_pos", pos_);
  cur_ = pick;
  pack_->get(cur_, q_);
  // Antworten mischen
  for (int i = 0; i < 3; i++) slots_[i] = i;
  for (int i = 2; i > 0; i--) { const int j = int(rnd() * (i + 1)) % (i + 1); const int t = slots_[i]; slots_[i] = slots_[j]; slots_[j] = t; }
  chosen_ = -1;
  // Zeilen vorab umbrechen
  qSmall_ = false;
  nQ_ = wrapText(fonts::body, q_.q, 300, qLines_, 5);
  if (nQ_ < 0) { qSmall_ = true; nQ_ = wrapText(fonts::small, q_.q, 310, qLines_, 6); if (nQ_ < 0) nQ_ = 6; }
  nBig_ = wrapText(fonts::title, q_.q, 316, bigLines_, 8);
  if (nBig_ < 0) nBig_ = 8;
  nE_ = wrapText(fonts::small, q_.expl, 312, eLines_, 5);
  if (nE_ < 0) nE_ = 5;
  sinceAsk_ = 0;
  idle_ = 0;
  setPhase(ASK);
}

int Oracle::correctSlot() const {
  for (int i = 0; i < 3; i++) if (slots_[i] == q_.correct) return i;
  return 0;
}

void Oracle::answer(int slot) {
  chosen_ = slot;
  right_ = slots_[slot] == q_.correct;
  streak_ = right_ ? streak_ + 1 : 0;
  if (streak_ > best_) { best_ = streak_; store_.set("or_best", best_); }
  store_.set("or_ser", streak_);
  all_++; allRight_ += right_;
  store_.set("or_all", all_); store_.set("or_allr", allRight_);
  if (epoch_ > 0) {
    const int wk = weekOf(epoch_);
    if (wk != wkId_) { wkId_ = wk; wkRight_ = wkAll_ = 0; store_.set("or_wk", wk); }
    wkAll_++; wkRight_ += right_;
    store_.set("or_wn", wkAll_); store_.set("or_wr", wkRight_);
  }
  if (right_) {
    Synth::Voice a; a.wave = Synth::SINE; a.f0 = a.f1 = 784; a.dur = 0.14f; a.vol = 0.22f; synth_.play(a);
    Synth::Voice b = a; b.f0 = b.f1 = 1175; b.delay = 0.11f; b.dur = 0.35f; synth_.play(b);
  } else {
    Synth::Voice a; a.wave = Synth::TRI; a.f0 = 247; a.f1 = 185; a.dur = 0.35f; a.vol = 0.22f; synth_.play(a);
  }
  setPhase(ANSWERED);
}

void Oracle::statsText(char* buf, int n) const {
  if (epoch_ > 0 && weekOf(epoch_) == wkId_ && wkAll_ > 0) snprintf(buf, n, "Woche: %d von %d richtig · Serie %d", wkRight_, wkAll_, streak_);
  else if (all_ > 0) snprintf(buf, n, "Bisher %d von %d richtig · Serie %d", allRight_, all_, streak_);
  else buf[0] = 0;
}

void Oracle::settingText(int i, char* buf, int n) const {
  if (i == 0) snprintf(buf, n, "Thema: %s", cat_ < 0 || !pack_ ? "alle" : pack_->category(cat_));
  else if (i == 1) snprintf(buf, n, "Schwierigkeit: %s", diff_ < 0 ? "alle" : DIFF_NAMES[diff_]);
  else snprintf(buf, n, "Stündlich melden: %s", nudge_ ? "an" : "aus");
}

bool Oracle::settingTap(int i) {
  if (i == 0) {
    const int nc = pack_ ? pack_->categories() : 0;
    cat_ = cat_ + 1 >= nc ? -1 : cat_ + 1;
    store_.set("or_cat", cat_);
  } else if (i == 1) {
    diff_ = diff_ >= 2 ? -1 : diff_ + 1;
    store_.set("or_diff", diff_);
  } else {
    nudge_ = !nudge_;
    store_.set("or_nudge", nudge_);
  }
  return false;
}

void Oracle::enter(uint32_t) { idle_ = 0; }

void Oracle::tick(const TileInput& in) {
  const float dt = in.dt;
  t_ += dt;
  clock_ += dt;
  idle_ += dt;
  sinceAsk_ += dt;
  epoch_ = in.epoch;
  msgT_ = std::fmax(0, msgT_ - dt);
  pokeT_ = std::fmax(0, pokeT_ - dt);
  if (in.tap || in.shaking) idle_ = 0;

  switch (phase_) {
    case SLEEP:
      if (in.tap || in.shaking) {
        if (!pack_) { pokeT_ = 0.6f; msgT_ = 3.5f; break; }
        setPhase(WAKE);
        Synth::Voice v; v.wave = Synth::SINE; v.f0 = 300; v.f1 = 620; v.attack = 0.05f; v.dur = 0.45f; v.vol = 0.09f; synth_.play(v);
      }
      break;
    case WAKE:
      if (t_ > 0.75f) nextQuestion();
      break;
    case ASK:
      if (in.tap && t_ > 0.3f) {
        if (in.tapY < PY0 - 10) setPhase(BIG);
        else
          for (int i = 0; i < 3; i++)
            if (in.tapY >= PY0 + i * PGAP && in.tapY < PY0 + i * PGAP + PH && in.tapX > PX - 6 && in.tapX < PX + PW + 6) answer(i);
      }
      break;
    case ANSWERED:
      if (in.tap && t_ > 0.8f) nextQuestion();
      break;
    case BIG:
      if (in.tap && t_ > 0.3f) { phase_ = ASK; t_ = 1; }
      break;
  }
  if (phase_ != SLEEP && phase_ != WAKE && idle_ > IDLE_SLEEP) setPhase(SLEEP);
}

// ---------------- Zeichnen ----------------

void Oracle::drawOrb(Scene& s, float scale, float glow, float alpha) {
  const float cx = OX + 10 * std::sin(clock_ * 0.06f), cy = OY + 8 * std::sin(clock_ * 0.045f + 1);
  const float pulse = 0.5f + 0.5f * std::sin(clock_ * 1.3f);
  const float wob = pokeT_ > 0 ? std::sin(pokeT_ * 30) * pokeT_ * 0.15f : 0;
  const float r = OR * scale;
  s.glow(cx, cy, r * 1.7f, hex(0x4a34c0), (0.2f + 0.1f * pulse + glow) * alpha, r * 0.95f);
  s.ellipse(cx, cy, r * (1 + wob), r * (1 - wob), hex(0x2c2266), hex(0x120c2e), alpha);
  for (int i = 0; i < 3; i++) {   // Schwaden im Inneren
    const float an = clock_ * (0.5f + 0.17f * i) * (1 + 3 * glow) + i * 2.1f;
    const float rr = r * (0.35f + 0.12f * i);
    s.glow(cx + rr * std::sin(an), cy - rr * 0.6f * std::cos(an), r * 0.55f, i == 1 ? hex(0x38b8c8) : hex(0x8a5cff), (0.22f + 0.1f * pulse + glow) * alpha);
  }
  s.ellipse(cx - r * 0.35f, cy - r * 0.5f, r * 0.28f, r * 0.13f, hex(0xffffff), 0.12f * alpha);
  for (int i = 0; i < 5; i++) {   // Funkeln rundherum
    const float an = i * 1.3f + clock_ * 0.15f, k = std::fmod(clock_ * 0.4f + i * 0.37f, 1.f);
    s.glow(cx + (r + 30) * std::sin(an), cy - (r + 30) * std::cos(an), 9, hex(0xcfc4ff), 0.6f * std::sin(k * PIF) * alpha);
  }
}

void Oracle::drawQuestion(Scene& s, float a) {
  const fonts::Font& f = qSmall_ ? fonts::small : fonts::body;
  const float lh = qSmall_ ? 24.f : 30.f;
  const float y0 = 62 + (144 - nQ_ * lh) * 0.5f;
  for (int i = 0; i < nQ_; i++) s.text(f, qLines_[i], W / 2, y0 + i * lh, TXT, a, Align::Center);
}

void Oracle::drawAnswers(Scene& s, float a) {
  const int good = correctSlot();
  const float k = phase_ == ANSWERED ? smooth((t_ - 1.1f) / 0.4f) : 0;   // Übergang zur Erklärung
  for (int i = 0; i < 3; i++) {
    float y = PY0 + i * PGAP, alpha = a;
    Col bg = PILL, fg = TXT;
    if (phase_ == ANSWERED) {
      if (i == good) {
        const float on = i == chosen_ ? 1.f : smooth((t_ - 0.25f) / 0.25f);
        bg = mix(PILL, GOOD, on);
        fg = mix(TXT, hex(0xffffff), on);
        y = y + (PY0 - y) * k;   // rückt nach oben
      } else {
        if (i == chosen_) { bg = BAD; fg = hex(0xffffff); }
        alpha *= 1 - k;
      }
    }
    if (alpha <= 0.01f) continue;
    s.rrect(PX, y, PW, PH, PH / 2, bg, alpha);
    const char* txt = q_.a[slots_[i]];
    if (Scene::textW(fonts::body, txt) <= PW - 28) {
      s.text(fonts::body, txt, W / 2, y + (PH - fonts::body.lineH) / 2 + 1, fg, alpha, Align::Center);
    } else {
      char l[2][160];
      int n = wrapText(fonts::small, txt, int(PW - 28), l, 2);
      if (n < 0) n = 2;
      const float ty = y + (PH - n * 22) / 2 - 1;
      for (int j = 0; j < n; j++) s.text(fonts::small, l[j], W / 2, ty + j * 22, fg, alpha, Align::Center);
    }
  }
}

void Oracle::draw(Scene& s) {
  char buf[96];
  switch (phase_) {
    case SLEEP: {
      const bool wants = nudge_ && pack_ && sinceAsk_ > 3600;   // „Ich hätte da was“
      const float extra = wants ? 0.15f * (0.5f + 0.5f * std::sin(clock_ * 2.2f)) : 0;
      drawOrb(s, 1, extra, 1);
      if (msgT_ > 0) {
        const float a = std::fmin(1.f, msgT_ / 0.5f) * std::fmin(1.f, (3.5f - msgT_) / 0.3f);
        s.text(fonts::body, "Noch keine Fragen", W / 2, 352, TXT, a, Align::Center);
        s.text(fonts::body, "geladen", W / 2, 382, TXT, a, Align::Center);
        break;
      }
      // Texte wechseln sich ab und blenden aus (Einbrennschutz)
      const float k = std::fmod(clock_, 30.f);
      const float a1 = smooth((k - 1) / 0.8f) * (1 - smooth((k - 8) / 0.8f));
      const float a2 = smooth((k - 15) / 0.8f) * (1 - smooth((k - 22) / 0.8f));
      s.text(wants ? fonts::body : fonts::small, wants ? "Ich hätte da was …" : "Tippen weckt das Orakel", W / 2, wants ? 378 : 386, wants ? TXT : TXT_DIM, a1, Align::Center);
      statsText(buf, sizeof buf);
      if (buf[0]) s.text(fonts::small, buf, W / 2, 386, TXT_DIM, a2, Align::Center);
      break;
    }
    case WAKE: {
      const float u = clampf(t_ / 0.75f, 0, 1);
      drawOrb(s, 1 + 0.35f * smooth(u), 0.4f * u, 1 - smooth((u - 0.45f) / 0.55f));
      break;
    }
    case ASK:
    case ANSWERED: {
      const float a = phase_ == ASK ? smooth(t_ / 0.35f) : 1;
      // Kopfzeile: Thema oder Ergebnis
      if (phase_ == ASK) {
        snprintf(buf, sizeof buf, "%s · %s", q_.cat, DIFF_NAMES[q_.diff < 0 || q_.diff > 2 ? 1 : q_.diff]);
        s.text(fonts::small, buf, W / 2, 30, TXT_DIM, a, Align::Center);
      } else {
        if (right_) snprintf(buf, sizeof buf, streak_ > 1 ? "Richtig! Serie %d" : "Richtig!", streak_);
        else snprintf(buf, sizeof buf, "Leider nicht");
        s.text(fonts::body, buf, W / 2, 24, right_ ? GOOD_TXT : BAD_TXT, smooth(t_ / 0.2f), Align::Center);
      }
      drawQuestion(s, a);
      drawAnswers(s, a);
      if (phase_ == ANSWERED) {
        const float ea = smooth((t_ - 1.3f) / 0.5f);
        for (int i = 0; i < nE_; i++) s.text(fonts::small, eLines_[i], W / 2, 290 + i * 23, TXT, ea, Align::Center);
        const float ha = smooth((t_ - 3.f) / 0.6f) * (0.6f + 0.2f * std::sin(clock_ * 2));
        s.text(fonts::small, "Tippen: nächste Frage", W / 2, 410, TXT_DIM, ha, Align::Center);
      }
      break;
    }
    case BIG: {
      const float a = smooth(t_ / 0.3f);
      const float lh = 40, y0 = (H - nBig_ * lh) * 0.5f - 10;
      for (int i = 0; i < nBig_; i++) s.text(fonts::title, bigLines_[i], W / 2, y0 + i * lh, TXT, a, Align::Center);
      s.text(fonts::small, "Tippen: zurück zu den Antworten", W / 2, 404, TXT_DIM, 0.7f * a, Align::Center);
      break;
    }
  }
}

}  // namespace dk
