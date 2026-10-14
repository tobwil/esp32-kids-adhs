#include "riddle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fonts.h"
#include "icons.h"
#include "pet.h"
#include "rewards.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4), PILL = hex(0x2c2848), PILL_RIM = hex(0x4a4470);
static const Col GOOD = hex(0x4cc47a), GOOD_RIM = hex(0x8ee8ae), GOOD_TXT = hex(0xeafff0), ACCENT = hex(0xb7a6ff), INK = hex(0x1a1630);

// Aufteilung (siehe auch Simulator: Textprüfung)
static constexpr float DOTS_Y = 30;
static constexpr float Q_Y = 72, Q_W = 296;                          // Textfrage, bis zu 3 Zeilen Titelschrift
static constexpr float ANS_X = 44, ANS_W = W - 88, ANS_Y = 214, ANS_H = 56, ANS_GAP = 10;
static constexpr float CNT_Q_Y = 64, CNT_CY = 200, CNT_DX = 64, CNT_DY = 58, CNT_R = 21;
static constexpr float NUM_Y = 316, NUM_W = 88, NUM_H = 72, NUM_DX = 104;
static constexpr float PHOTO_Y = 18, HQ_Y = 222, H_ANS_Y = 262, H_ANS_H = 48, H_ANS_GAP = 8;   // Foto oben wie im Album
static constexpr float RIGHT_SECS = 1.5f, DONE_SECS = 6;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

float Riddle::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return (seed_ & 0xFFFFFF) / float(0x1000000);
}

void Riddle::start() {
  phase_ = ASK;
  t_ = 0;
  qNo_ = solved_ = 0;
  const bool horse = album_ && img_ && album_->owned() >= 3;
  nQ_ = PER_ROUND + (horse ? 1 : 0);
  next();
}

void Riddle::next() {
  chosen_ = -1;
  counted_ = 0;
  t_ = 0;
  if (qNo_ >= nQ_) {
    phase_ = DONE;
    stars_ = starsAdd(store_);
    petEvent(store_, "pe_play");
    Synth::Voice v; v.wave = Synth::SINE; v.vol = 0.12f; v.dur = 0.25f;
    for (int k = 0; k < 3; k++) { v.f0 = v.f1 = 523.f * std::pow(1.26f, float(k)); v.delay = 0.12f * k; synth_.play(v); }
    return;
  }
  const bool ok = qNo_ == PER_ROUND ? pickHorse() : pickQuestion();
  if (!ok) { nQ_ = qNo_; next(); return; }   // nichts mehr da: Runde endet hier
  phase_ = ASK;
}

bool Riddle::pickQuestion() {
  if (!pack_ || pack_->count() == 0) return false;
  int pick = -1;
  for (int tries = 0; tries < 40 && pick < 0; tries++) {
    const int i = int(rnd() * pack_->count()) % pack_->count();
    bool seen = false;
    for (int k = 0; k < nRecent_ && k < 16; k++) seen |= recent_[k] == i;
    if (seen && tries < 30) continue;
    if (pack_->categoryOf(i) == lastCat_ && tries < 20) continue;   // Abwechslung
    pick = i;
  }
  Question q;
  if (pick < 0 || !pack_->get(pick, q)) return false;
  recent_[nRecent_++ % 16] = pick;
  lastCat_ = pack_->categoryOf(pick);

  snprintf(q_, sizeof q_, "%s", q.q);
  snprintf(expl_, sizeof expl_, "%s", q.expl);
  // Antworten mischen
  int ord[3] = {0, 1, 2};
  for (int k = 2; k > 0; k--) { const int j = int(rnd() * (k + 1)) % (k + 1); const int t = ord[k]; ord[k] = ord[j]; ord[j] = t; }
  for (int k = 0; k < 3; k++) {
    snprintf(a_[k], sizeof a_[k], "%s", q.a[ord[k]]);
    if (ord[k] == q.correct) correct_ = k;
  }
  // Zählbild „zaehl:N:ding“; bei Zählfragen ordnen sich die Zahlen aufsteigend (leichter zu finden)
  kind_ = TEXT;
  if (!std::strncmp(q.pic, "zaehl:", 6)) {
    const int n = std::atoi(q.pic + 6);
    const char* what = std::strchr(q.pic + 6, ':');
    if (n >= 1 && n <= 12 && what) {
      kind_ = COUNT;
      countN_ = n;
      what++;
      countWhat_ = !std::strcmp(what, "stern") ? 1 : !std::strcmp(what, "herz") ? 2 : !std::strcmp(what, "ball") ? 3 : 0;
      char tmp[40];
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2 - i; j++)
          if (std::atoi(a_[j]) > std::atoi(a_[j + 1])) { std::memcpy(tmp, a_[j], 40); std::memcpy(a_[j], a_[j + 1], 40); std::memcpy(a_[j + 1], tmp, 40); }
      for (int k = 0; k < 3; k++) if (std::atoi(a_[k]) == n) correct_ = k;   // tools/quiz.py prüft: richtige Antwort = Anzahl
    }
  }
  return true;
}

bool Riddle::pickHorse() {
  const int n = album_->owned();
  if (n < 3) return false;
  int ks[3];
  ks[0] = int(rnd() * n) % n;
  do ks[1] = int(rnd() * n) % n; while (ks[1] == ks[0]);
  do ks[2] = int(rnd() * n) % n; while (ks[2] == ks[0] || ks[2] == ks[1]);
  horse_ = album_->ownedCard(ks[0]);
  const Card& c = album_->cardInfo(horse_);
  if (!img_->card(c.id).px) return false;   // ohne Foto kein Bilderrätsel
  kind_ = HORSE;
  snprintf(q_, sizeof q_, "%s", album_->question());
  if (*c.sub && *c.line1) snprintf(expl_, sizeof expl_, "%s · %s", c.sub, c.line1);
  else snprintf(expl_, sizeof expl_, "%s", *c.sub ? c.sub : c.line1);
  correct_ = int(rnd() * 3) % 3;
  for (int k = 0, d = 1; k < 3; k++) {
    const int ci = k == correct_ ? horse_ : album_->ownedCard(ks[d++]);
    snprintf(a_[k], sizeof a_[k], "%s", album_->cardInfo(ci).name);
  }
  return true;
}

void Riddle::answerRect(int i, float& x, float& y, float& w, float& h) const {
  if (kind_ == COUNT) { w = NUM_W; h = NUM_H; x = W / 2 + (i - 1) * NUM_DX - NUM_W / 2; y = NUM_Y; return; }
  x = ANS_X; w = ANS_W;
  if (kind_ == HORSE) { h = H_ANS_H; y = H_ANS_Y + i * (H_ANS_H + H_ANS_GAP); return; }
  h = ANS_H; y = ANS_Y + i * (ANS_H + ANS_GAP);
}

void Riddle::countPos(int i, float& x, float& y) const {
  const int n = countN_;
  const int cols = n <= 4 ? n : n <= 6 || n == 9 ? 3 : 4;
  const int rows = (n + cols - 1) / cols, r = i / cols;
  const int inRow = r < rows - 1 ? cols : n - cols * (rows - 1);
  x = W / 2 + (i % cols - (inRow - 1) * 0.5f) * CNT_DX;
  y = CNT_CY + (r - (rows - 1) * 0.5f) * CNT_DY;
}

void Riddle::answer(int i) {
  chosen_ = i;
  t_ = 0;
  Synth::Voice v; v.wave = Synth::SINE; v.dur = 0.18f;
  if (i == correct_) {
    phase_ = RIGHT;
    solved_++;
    v.vol = 0.12f;
    v.f0 = v.f1 = 660; synth_.play(v);
    v.f0 = v.f1 = 880; v.delay = 0.1f; synth_.play(v);
  } else {
    phase_ = WRONG;   // kein Fehlerton, nur ein leises Summen
    v.vol = 0.05f; v.f0 = 392; v.f1 = 330; v.dur = 0.3f; synth_.play(v);
  }
}

void Riddle::tick(const TileInput& in) {
  t_ += in.dt;
  clock_ += in.dt;
  switch (phase_) {
    case IDLE:
      if (in.tap) start();
      break;
    case ASK:
      if (!in.tap || t_ < 0.3f) break;
      for (int i = 0; i < 3; i++) {
        float x, y, w, h;
        answerRect(i, x, y, w, h);
        if (in.tapX > x - 6 && in.tapX < x + w + 6 && in.tapY > y - 4 && in.tapY < y + h + 4) { answer(i); return; }
      }
      if (kind_ == COUNT)   // Zählhilfe: angetippte Dinge bekommen einen Haken
        for (int i = 0; i < countN_; i++) {
          float x, y;
          countPos(i, x, y);
          if ((in.tapX - x) * (in.tapX - x) + (in.tapY - y) * (in.tapY - y) < (CNT_R + 9) * (CNT_R + 9)) { counted_ ^= uint16_t(1u << i); break; }
        }
      break;
    case RIGHT:
      if (t_ > RIGHT_SECS || (in.tap && t_ > 0.4f)) { qNo_++; next(); }
      break;
    case WRONG:
      if (in.tap && t_ > 0.8f) { qNo_++; next(); }
      break;
    case DONE:
      if (t_ > DONE_SECS || (in.tap && t_ > 1)) { phase_ = IDLE; t_ = 0; requestGoto(GOTO_BACK); }
      break;
  }
}

// ---------------- Zeichnen ----------------

static void drawThing(Scene& s, int what, float x, float y, float r, float a) {
  switch (what) {
    case 1: starIcon(s, x, y, r * 1.05f, a); break;
    case 2: heart(s, x, y - r * 0.1f, r * 0.95f, hex(0xff7a9a), a); break;
    case 3: ball(s, x, y, r * 0.9f, a); break;
    default: apple(s, x, y, r, a); break;
  }
}

static void pill(Scene& s, float x, float y, float w, float h, const char* label, const fonts::Font& f, bool good, float a) {
  s.rrect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, good ? GOOD_RIM : PILL_RIM, a);
  s.rrect(x, y, w, h, h / 2, good ? GOOD : PILL, a);
  const Col fg = good ? GOOD_TXT : TXT;
  s.text(f, label, x + w / 2, y + (h - f.lineH) / 2 + 1, fg, a, Align::Center);
}

void Riddle::draw(Scene& s) {
  if (phase_ == IDLE) {
    s.text(fonts::title, "Rätsel", W / 2, 150, TXT, 1, Align::Center);
    s.text(fonts::body, "Tippen zum Starten", W / 2, 210, TXT_DIM, 1, Align::Center);
    return;
  }
  if (phase_ == DONE) {
    const float a = smooth(t_ / 0.5f);
    const float pulse = 1 + 0.04f * std::sin(clock_ * 2.2f);
    s.glow(W / 2, 150, 90, hex(0xffd166), 0.25f * a, 40);
    s.star(W / 2, 150, 54 * pulse * (0.6f + 0.4f * a), hex(0xffd166), a);
    s.text(fonts::title, "Geschafft!", W / 2, 236, TXT, a, Align::Center);
    char b[48];
    if (solved_ == 1) snprintf(b, sizeof b, "Ein Rätsel gelöst!");
    else if (solved_ > 1) snprintf(b, sizeof b, "%d Rätsel gelöst!", solved_);
    else snprintf(b, sizeof b, "Danke fürs Mitraten!");
    s.text(fonts::body, b, W / 2, 286, ACCENT, a, Align::Center);
    drawStarCount(s, W / 2, 340, stars_, a);
    return;
  }

  const float fadeIn = phase_ == ASK ? smooth(t_ / 0.25f) : 1;

  // Fortschritt: ein Punkt je Frage (nicht bei der Pferdefrage, dort liegt oben das Foto)
  if (kind_ != HORSE) {
    const float x0 = W / 2 - (nQ_ - 1) * 9.f;
    for (int i = 0; i < nQ_; i++) {
      const bool done = i < qNo_, cur = i == qNo_;
      s.circle(x0 + i * 18, DOTS_Y, cur ? 6.f : 4.5f, done || cur ? ACCENT : hex(0x3a3658), cur ? 1.f : done ? 0.8f : 1.f);
    }
  }

  if (kind_ == HORSE) {
    s.text(fonts::body, q_, W / 2, HQ_Y, TXT, fadeIn, Align::Center);
    const Image im = img_ ? img_->card(album_->cardInfo(horse_).id) : Image{};
    if (im.px) s.image(im, (W - CARD_W) / 2, PHOTO_Y, 22, fadeIn);
  } else if (kind_ == COUNT) {
    s.text(fonts::body, q_, W / 2, CNT_Q_Y, TXT, fadeIn, Align::Center);
    for (int i = 0; i < countN_; i++) {
      float x, y;
      countPos(i, x, y);
      const bool marked = counted_ >> i & 1;
      const float bob = 2.f * std::sin(clock_ * 1.3f + i * 0.9f);
      drawThing(s, countWhat_, x, y + bob, CNT_R, fadeIn * (marked ? 0.45f : 1.f));
      if (phase_ == WRONG) {
        // Lösung: Grummel zählt mit, die Zahlen erscheinen nacheinander
        const float ap = smooth((t_ - 0.3f - i * 0.35f) / 0.25f);
        if (ap > 0) {
          char b[4];
          snprintf(b, sizeof b, "%d", i + 1);
          s.circle(x, y + bob, 12, INK, 0.85f * ap);
          s.text(fonts::small, b, x, y + bob - fonts::small.lineH / 2.f, TXT, ap, Align::Center);
        }
      } else if (marked) {
        s.capsule(x - 7, y + bob, x - 2, y + bob + 6, 2.4f, GOOD_TXT, fadeIn);
        s.capsule(x - 2, y + bob + 6, x + 8, y + bob - 6, 2.4f, GOOD_TXT, fadeIn);
      }
    }
    if (phase_ == ASK) s.text(fonts::small, "Antippen hilft beim Zählen", W / 2, NUM_Y + NUM_H + 16, scale(TXT_DIM, 0.8f), fadeIn * smooth((t_ - 2) / 1), Align::Center);
  } else {
    char lines[3][160];
    const fonts::Font* f = &fonts::title;
    int n = wrapText(*f, q_, int(Q_W), lines, 3);
    if (n < 0) { f = &fonts::body; n = wrapText(*f, q_, int(Q_W), lines, 3); }
    if (phase_ == WRONG) {
      // Frage oben klein lassen, darunter die Lösung mit Erklärung
      s.text(fonts::title, "Fast! Richtig ist:", W / 2, 96, TXT, 1, Align::Center);
    } else {
      const float y0 = Q_Y + (3 * fonts::title.lineH - n * f->lineH) * 0.5f;   // mittig im Fragenbereich
      for (int i = 0; i < n; i++) s.text(*f, lines[i], W / 2, y0 + i * f->lineH, TXT, fadeIn, Align::Center);
    }
  }

  // Antworten
  if (phase_ == WRONG) {
    float x, y, w, h;
    answerRect(kind_ == COUNT ? correct_ : 0, x, y, w, h);
    if (kind_ == TEXT) y = 150;
    if (kind_ == COUNT) { x = ANS_X; w = ANS_W; h = 60; }
    char b[64];
    if (kind_ == COUNT) snprintf(b, sizeof b, "Es sind %s.", a_[correct_]);
    else snprintf(b, sizeof b, "%s", a_[correct_]);
    const float pa = smooth(t_ / 0.3f);
    pill(s, x, y, w, h, b, fonts::body, true, pa);
    if (kind_ != COUNT) {
      char lines[5][160];
      const int n = wrapText(fonts::small, expl_, int(W - 80), lines, 5);
      const float ey = y + h + 16;
      for (int i = 0; i < n; i++) s.text(fonts::small, lines[i], W / 2, ey + i * fonts::small.lineH, TXT_DIM, pa, Align::Center);
    }
    s.text(fonts::small, "Tippen = weiter", W / 2, 412, scale(TXT_DIM, 0.8f), smooth((t_ - 0.8f) / 0.4f), Align::Center);
    return;
  }
  for (int i = 0; i < 3; i++) {
    float x, y, w, h;
    answerRect(i, x, y, w, h);
    const bool right = phase_ == RIGHT && i == correct_;
    const float a = fadeIn * (phase_ == RIGHT && !right ? 0.35f : 1.f);
    const fonts::Font& f = kind_ == COUNT ? fonts::title : fonts::body;
    pill(s, x, y, w, h, a_[i], f, right, a);
    if (right) {   // kleine Sterne steigen auf
      for (int k = 0; k < 3; k++) {
        const float u = clampf(t_ / 1.2f - k * 0.12f, 0, 1);
        if (u <= 0 || u >= 1) continue;
        s.star(x + w * (0.25f + 0.25f * k), y - 24 * u, 7 * (1 - u * 0.5f), hex(0xffd166), 1 - u);
      }
    }
  }
}

}  // namespace dk
