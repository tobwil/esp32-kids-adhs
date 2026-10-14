#include "album.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"
#include "horses.h"
#include "quiz.h"
#include "rewards.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4), GOLD = hex(0xffd166);
static const Col CELL = hex(0x1b2540), CELL_LOCKED = hex(0x121828), CARD_BG = hex(0x182238), CARD_RIM = hex(0x34477a);
static constexpr int PER_PAGE = 4;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

// Silhouette für noch nicht gesammelte Karten und Platzhalter ohne Foto
static const Horse SHADOW = {"", 150, Coat::BROWN, 0x26304c, 0x1e2640, false};
static const Horse PLACEHOLDER = {"", 150, Coat::BROWN, 0x8a5a3a, 0x3a2418, false};

Album::Album(Synth& synth, Store& store, uint32_t seed) : synth_(synth), store_(store), seed_(seed | 1) {}

bool Album::load(const char* text) {
  cards_.clear();
  text_.assign(text, text + std::strlen(text) + 1);
  // Kopfzeilen: Voreinstellungen sind die Pferde
  snprintf(id_, sizeof id_, "pferde");
  title_[0] = short_[0] = count_[0] = question_[0] = 0;
  bool v2 = false;
  static char empty[1] = "";
  char* p = text_.data();
  while (*p) {
    char* e = std::strchr(p, '\n');
    if (e) *e = 0;
    if (*p == '#') {
      char* val = std::strchr(p, ' ');
      if (val) {
        *val++ = 0;
        if (!std::strcmp(p, "#album")) snprintf(id_, sizeof id_, "%s", val);
        else if (!std::strcmp(p, "#titel")) snprintf(title_, sizeof title_, "%s", val);
        else if (!std::strcmp(p, "#symbol")) snprintf(short_, sizeof short_, "%s", val);
        else if (!std::strcmp(p, "#zaehlwort")) snprintf(count_, sizeof count_, "%s", val);
        else if (!std::strcmp(p, "#frage")) snprintf(question_, sizeof question_, "%s", val);
        else if (!std::strcmp(p, "#format")) v2 = std::atoi(val) >= 2;
      }
    } else if (*p && int(cards_.size()) < MAX_CARDS) {
      char* f[11] = {};
      int n = 0;
      f[n++] = p;
      for (char* q = p; *q && n < 11; q++)
        if (*q == '|') { *q = 0; f[n++] = q + 1; }
      for (int k = n; k < 11; k++) f[k] = empty;
      if (v2 && n >= 2) cards_.push_back({std::atoi(f[0]), f[1], f[2], f[3], empty, f[4], f[5], f[6], f[7], f[8], f[9], f[10]});
      else if (!v2 && n >= 8) {   // Pferdeformat
        static char gr[] = "Größe", fa[] = "Farben", we[] = "Wesen";
        cards_.push_back({std::atoi(f[0]), f[1], f[2], f[3], gr, f[4], fa, f[5], we, f[6], f[7], f[8]});
      }
    }
    if (!e) break;
    p = e + 1;
  }
  horses_ = !std::strcmp(id_, "pferde");
  if (!title_[0]) snprintf(title_, sizeof title_, horses_ ? "Pferde-Album" : "Sammelalbum");
  if (!short_[0]) snprintf(short_, sizeof short_, horses_ ? "Pferde" : "Album");
  if (!count_[0]) snprintf(count_, sizeof count_, horses_ ? "Pferden" : "Karten");
  if (!question_[0]) snprintf(question_, sizeof question_, horses_ ? "Welches Pferd ist das?" : "Wer ist das?");
  // Sammelstand je Thema; die Pferde behalten ihre alten Schlüssel
  if (horses_) {
    snprintf(kOpen_, sizeof kOpen_, "alb_open"); snprintf(kSeed_, sizeof kSeed_, "alb_seed"); kBase_[0] = 0;
  } else {
    uint32_t h = 2166136261u;
    for (const char* c = id_; *c; c++) h = (h ^ uint8_t(*c)) * 16777619u;
    snprintf(kOpen_, sizeof kOpen_, "ao%06lx", (unsigned long)(h & 0xFFFFFF));
    snprintf(kSeed_, sizeof kSeed_, "as%06lx", (unsigned long)(h & 0xFFFFFF));
    snprintf(kBase_, sizeof kBase_, "ab%06lx", (unsigned long)(h & 0xFFFFFF));
  }
  // neues Thema beginnt bei null: Sterne, die es vorher schon gab, zählen nicht
  base_ = 0;
  if (kBase_[0]) {
    base_ = store_.get(kBase_, -1);
    if (base_ < 0) { base_ = starsGet(store_); store_.set(kBase_, base_); }
  }
  store_.set("alb_tot", count());
  shuffle();
  opened_ = store_.get(kOpen_, 0);
  if (opened_ > count()) opened_ = count();
  page_ = 0;
  phase_ = GRID;
  return !cards_.empty();
}

// Freischalt-Reihenfolge: einmal gemischt, Startwert im NVS, damit nach Neustart nichts doppelt kommt
void Album::shuffle() {
  uint32_t x = uint32_t(store_.get(kSeed_, 0));
  if (!x) { x = (seed_ & 0x7FFFFFFF) | 1; store_.set(kSeed_, int(x)); }
  const int n = count();
  order_.resize(n);
  for (int i = 0; i < n; i++) order_[i] = i;
  for (int i = n - 1; i > 0; i--) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    const int j = int(x % uint32_t(i + 1));
    const int t = order_[i]; order_[i] = order_[j]; order_[j] = t;
  }
}

bool Album::isOwned(int i) const {
  for (int k = 0; k < opened_; k++) if (order_[k] == i) return true;
  return false;
}

int Album::pending() const {
  const int st = starsGet(store_) - base_;
  const int earned = st > 0 ? st / STARS_PER_CARD : 0;
  const int e = earned < count() ? earned : count();
  return e > opened_ ? e - opened_ : 0;
}

void Album::cellRect(int slot, float& x, float& y, float& w, float& h) {
  const int k = slot % PER_PAGE;
  w = THUMB_W; h = THUMB_H + 32;
  x = k % 2 ? 186 : 18;
  y = 84 + (k / 2) * 148;
}

void Album::enter(uint32_t) {
  phase_ = pending() > 0 ? WRAPPED : GRID;
  t_ = 0;
}

void Album::tick(const TileInput& in) {
  t_ += in.dt;
  clock_ += in.dt;
  hintT_ = std::fmax(0, hintT_ - in.dt);
  switch (phase_) {
    case GRID: {
      if (pending() > 0 && t_ > 0.3f) { phase_ = WRAPPED; t_ = 0; break; }
      if (!in.tap || t_ < 0.2f) break;
      const int pages = (count() + PER_PAGE - 1) / PER_PAGE;
      if (in.tapY > 380) {   // Blättern
        if (in.tapX < 120 && page_ > 0) { page_--; t_ = 0; }
        else if (in.tapX > W - 120 && page_ < pages - 1) { page_++; t_ = 0; }
        break;
      }
      for (int k = 0; k < PER_PAGE; k++) {
        const int i = page_ * PER_PAGE + k;
        if (i >= count()) break;
        float x, y, w, h;
        cellRect(k, x, y, w, h);
        if (in.tapX >= x && in.tapX < x + w && in.tapY >= y && in.tapY < y + h) {
          if (isOwned(i)) { card_ = i; phase_ = FRONT; t_ = 0; }
          else hintT_ = 3;
        }
      }
      break;
    }
    case FRONT:
      if (in.tap && t_ > 0.3f) { phase_ = BACK; t_ = 0; }
      break;
    case BACK:
      if (in.tap && t_ > 0.3f) { phase_ = GRID; t_ = 0; page_ = card_ / PER_PAGE; }
      break;
    case WRAPPED:
      if (in.tap && t_ > 0.6f) {
        // nächste Karte aus der gemischten Reihenfolge aufdecken
        if (opened_ >= count()) { phase_ = GRID; break; }
        card_ = order_[opened_];
        opened_++;
        store_.set(kOpen_, opened_);
        phase_ = REVEAL; t_ = 0;
        for (int k = 0; k < 4; k++) { Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = 523.f * std::pow(1.26f, float(k)); v.delay = 0.09f * k; v.dur = 0.35f; v.vol = 0.12f; synth_.play(v); }
      }
      break;
    case REVEAL:
      if (in.tap && t_ > 1.0f) { phase_ = BACK; t_ = 0; }
      break;
  }
}

// ---------------- Zeichnen ----------------

static void wrapped(Scene& s, const char* text, const fonts::Font& f, float y, float lh, int maxLines, Col c, float a, int maxW = 320) {
  char l[4][160];
  int n = wrapText(f, text, maxW, l, maxLines);
  if (n < 0) n = maxLines;
  for (int i = 0; i < n; i++) s.text(f, l[i], W / 2, y + i * lh, c, a, Align::Center);
}

void Album::drawGrid(Scene& s) {
  char buf[48];
  s.text(Scene::textW(fonts::title, title_) <= 300 ? fonts::title : fonts::body, title_, W / 2, 20, TXT, 1, Align::Center);
  snprintf(buf, sizeof buf, "%d von %d %s", opened_, count(), count_);
  s.text(fonts::small, buf, W / 2, 56, TXT_DIM, 1, Align::Center);
  const float pa = smooth(t_ / 0.25f);
  for (int k = 0; k < PER_PAGE; k++) {
    const int i = page_ * PER_PAGE + k;
    if (i >= count()) break;
    float x, y, w, h;
    cellRect(k, x, y, w, h);
    const bool have = isOwned(i);
    s.rrect(x, y, w, h, 14, have ? CELL : CELL_LOCKED, pa);
    if (have) {
      const Image im = img_ ? img_->thumb(cards_[i].id) : Image{};
      if (im.px) s.image(im, x, y, 14, pa);
      else drawPlaceholder(s, x + w / 2, y + 50, 0.62f, false, pa);
      const fonts::Font& f = Scene::textW(fonts::body, cards_[i].name) <= w - 10 ? fonts::body : fonts::small;
      s.text(f, cards_[i].name, x + w / 2, y + THUMB_H + 2 + (&f == &fonts::body ? 0 : 3), TXT, pa, Align::Center);
    } else {
      drawPlaceholder(s, x + w / 2, y + 50, 0.62f, true, pa);
      s.text(fonts::title, "?", x + w / 2, y + 26, hex(0x4a5a8a), pa, Align::Center);
      snprintf(buf, sizeof buf, "%d", cards_[i].id);
      s.text(fonts::small, buf, x + w / 2, y + THUMB_H + 5, hex(0x4a5a8a), pa, Align::Center);
    }
  }
  // Blättern
  const int pages = (count() + PER_PAGE - 1) / PER_PAGE;
  for (int side = 0; side < 2; side++) {
    if ((side == 0 && page_ == 0) || (side == 1 && page_ >= pages - 1)) continue;
    const float bx = side ? W - 70 : 70, by = 404, d = side ? 1 : -1;
    s.circle(bx, by, 22, hex(0x2a3658));
    s.capsule(bx + 5 * d, by, bx - 4 * d, by - 9, 3, TXT);
    s.capsule(bx + 5 * d, by, bx - 4 * d, by + 9, 3, TXT);
  }
  for (int p = 0; p < pages; p++) s.circle(W / 2 + (p - (pages - 1) * 0.5f) * 14, 404, p == page_ ? 4.5f : 3, p == page_ ? TXT : hex(0x4a5070));
  if (hintT_ > 0) {
    const int need = STARS_PER_CARD - starsGet(store_) % STARS_PER_CARD;
    snprintf(buf, sizeof buf, need == 1 ? "Noch 1 Stern bis zur nächsten Karte" : "Noch %d Sterne bis zur nächsten Karte", need);
    const float a = std::fmin(1.f, hintT_ / 0.4f);
    s.rrect(24, 346, W - 48, 34, 17, hex(0x2a2440), a);
    s.text(fonts::small, buf, W / 2, 352, GOLD, a, Align::Center);
  }
}

// Ohne Foto: gezeichnetes Pferd (eingebaute Pferde) oder ein Stern; gesperrt: Schatten
void Album::drawPlaceholder(Scene& s, float x, float y, float scale, bool locked, float a) {
  if (horses_) {
    drawHorse(s, locked ? SHADOW : PLACEHOLDER, x - 6 * scale / 0.62f, y + 38 * scale / 0.62f, scale, locked ? 0 : clock_ * 0.3f, a);
    return;
  }
  const float r = 40 * scale / 0.62f;
  s.star(x, y + 4, r * 0.62f, locked ? hex(0x26304c) : GOLD, a, locked ? 0 : 0.2f * std::sin(clock_));
}

void Album::drawFront(Scene& s, int i, float a) {
  const Card& c = cards_[i];
  s.rrect(18, 18, W - 36, H - 36, 44, CARD_RIM, a);
  s.rrect(20, 20, W - 40, H - 40, 42, CARD_BG, a);
  const Image im = img_ ? img_->card(c.id) : Image{};
  if (im.px) s.image(im, 20, 20, 42, a);
  else drawPlaceholder(s, W / 2, 122, 1.2f, false, a);
  const fonts::Font& nf = Scene::textW(fonts::title, c.name) <= 310 ? fonts::title : fonts::body;
  s.text(nf, c.name, W / 2, 226, TXT, a, Align::Center);
  if (*c.sub) s.text(fonts::body, c.sub, W / 2, 266, GOLD, a, Align::Center);
  if (*c.line1) wrapped(s, c.line1, fonts::body, 296, 28, 2, TXT_DIM, a);
  if (*c.line2) {
    char buf[96];
    if (*c.label2) snprintf(buf, sizeof buf, "%s: %s", c.label2, c.line2);
    else snprintf(buf, sizeof buf, "%s", c.line2);
    wrapped(s, buf, fonts::body, 344, 28, 1, TXT, a, 316);
  }
  const float ha = (0.5f + 0.25f * std::sin(clock_ * 2.5f)) * smooth((t_ - 1.5f) / 0.5f);
  s.text(fonts::small, "Tippen zum Umdrehen", W / 2, 392, TXT_DIM, ha * a, Align::Center);
}

void Album::drawBack(Scene& s, int i, float a) {
  const Card& c = cards_[i];
  s.rrect(18, 18, W - 36, H - 36, 44, CARD_RIM, a);
  s.rrect(20, 20, W - 40, H - 40, 42, CARD_BG, a);
  // langer Name: kleinere Schrift, damit er auf die Karte passt
  if (Scene::textW(fonts::title, c.name) <= 226) s.text(fonts::title, c.name, W / 2, 34, TXT, a, Align::Center);
  else s.text(fonts::body, c.name, W / 2, 40, TXT, a, Align::Center);
  if (*c.head3 || *c.text3) {
    s.text(fonts::body, c.head3, W / 2, 84, GOLD, a, Align::Center);
    wrapped(s, c.text3, fonts::small, 112, 22, 2, TXT, a, 300);
  }
  if (*c.head4 || *c.text4) {
    s.text(fonts::body, c.head4, W / 2, 164, GOLD, a, Align::Center);
    wrapped(s, c.text4, fonts::small, 192, 22, 2, TXT, a, 300);
  }
  if (*c.fact) {
    s.star(W / 2 - Scene::textW(fonts::body, "Wusstest du?") / 2 - 16, 258, 9, GOLD, a);
    s.text(fonts::body, "Wusstest du?", W / 2 + 6, 244, GOLD, a, Align::Center);
    // groß, wenn es in drei Zeilen passt, sonst kleiner
    char l[4][160];
    if (wrapText(fonts::body, c.fact, 296, l, 3) > 0) wrapped(s, c.fact, fonts::body, 274, 28, 3, TXT, a, 296);
    else wrapped(s, c.fact, fonts::small, 276, 23, 4, TXT, a, 300);
  }
  if (*c.credit) wrapped(s, c.credit, fonts::small, 372, 20, 2, hex(0x6a6a8a), a, 270);
}

void Album::drawWrapped(Scene& s, float a) {
  // verdeckte Karte mit Glitzer
  const float bob = 4 * std::sin(clock_ * 2), cx = W / 2, cy = 214 + bob;
  s.glow(cx, cy, 190, hex(0x6a4cff), 0.25f * a, 120);
  s.rrect(cx - 110, cy - 140, 220, 280, 26, hex(0x5a3fb8), a);
  s.rrect(cx - 100, cy - 130, 200, 260, 20, hex(0x3a2a80), a);
  s.text(fonts::big, "?", cx, cy - 48, hex(0xffe08a), a, Align::Center);
  for (int k = 0; k < 8; k++) {
    const float an = k * TAU / 8 + clock_ * 0.6f, rr = 150 + 10 * std::sin(clock_ * 3 + k);
    s.star(cx + rr * std::sin(an), cy - rr * 1.05f * std::cos(an), 7 + 2 * std::sin(clock_ * 4 + k), GOLD, 0.8f * a, clock_ + k);
  }
  s.text(fonts::title, "Neue Karte!", W / 2, 20, TXT, a, Align::Center);
  const float ha = 0.6f + 0.4f * std::sin(clock_ * 3);
  s.text(fonts::body, "Tippen zum Aufdecken", W / 2, 384, GOLD, ha * a, Align::Center);
}

void Album::draw(Scene& s) {
  switch (phase_) {
    case GRID: drawGrid(s); break;
    case FRONT: drawFront(s, card_, smooth(t_ / 0.25f)); break;
    case BACK: drawBack(s, card_, smooth(t_ / 0.25f)); break;
    case WRAPPED: drawWrapped(s, smooth(t_ / 0.3f)); break;
    case REVEAL: {
      drawFront(s, card_, smooth(t_ / 0.35f));
      for (int k = 0; k < 10; k++) {   // Sterne fliegen auseinander
        const float an = k * TAU / 10, rr = 40 + 220 * smooth(t_ / 1.2f);
        s.star(W / 2 + rr * std::sin(an), 120 - rr * std::cos(an), 9, GOLD, 1 - smooth((t_ - 0.6f) / 0.6f), an + t_ * 3);
      }
      const float na = smooth((t_ - 0.3f) / 0.3f) * (1 - smooth((t_ - 2.2f) / 0.4f));
      s.rrect(W / 2 - 64, 22, 128, 36, 18, hex(0xffd166), na);
      s.text(fonts::body, "Neu!", W / 2, 25, hex(0x2a1a00), na, Align::Center);
      break;
    }
  }
}

}  // namespace dk
