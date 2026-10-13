#include "checkin.h"

#include <cmath>
#include <cstdio>

#include "fonts.h"
#include "quiz.h"
#include "rewards.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4), INK = hex(0x2a1a24);

struct FeelingInfo {
  const char* label;
  Col top, bottom;
  const char* say1;   // große Zeile
  const char* say2;   // kleine Zeile
};
static const FeelingInfo FEEL[CheckIn::N_FEELINGS] = {
    {"wütend", hex(0xff9a76), hex(0xe2553a), "Wut im Bauch?", "Grummel frisst sie auf!"},
    {"kribbelig", hex(0xffe08a), hex(0xf2b640), "Ganz kribbelig?", "Wir atmen zusammen."},
    {"traurig", hex(0x9cc4ff), hex(0x5a8ae0), "Traurig sein ist okay.", "Atmen und Kuscheln helfen."},
    {"müde", hex(0xc9bcff), hex(0x8f7fe0), "Ein bisschen müde?", "Wir atmen ganz langsam."},
    {"froh", hex(0x9ff0b8), hex(0x4cc47a), "Juhu!", "Das freut mich!"},
};

static constexpr float FACE_R = 42, CHOSEN_R = 76, CHOSEN_Y = 196;
static constexpr float GO_AFTER = 3.2f, HAPPY_BACK = 3.8f;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

void CheckIn::faceCenter(int i, float& x, float& y) {
  static const float X[5] = {72, 184, 296, 128, 240}, Y[5] = {176, 176, 176, 318, 318};
  x = X[i]; y = Y[i];
}

CheckIn::CheckIn(Synth& synth, Store& store) : synth_(synth), store_(store) {}

void CheckIn::enter(uint32_t) {
  chosen_ = -1;
  again_ = false;
  t_ = 0;
  idle_ = 0;
}

// Verlauf: 30 Einträge im NVS-Ring, je Minute seit 1970 · 8 + Gefühl (0 Minuten = Uhrzeit unbekannt)
void CheckIn::log(int feeling, int64_t epoch) {
  const int i = store_.get("ci_n", 0) % 30;
  char key[8];
  snprintf(key, sizeof key, "ci%d", i);
  store_.set(key, int((epoch > 0 ? epoch / 60 : 0) * 8 + feeling));
  store_.set("ci_n", (i + 1) % 30);
}

// wartende Pferdekarten (alle 3 Sterne eine, siehe Album)
int CheckIn::cardsWaiting() const {
  const int tot = store_.get("alb_tot", 0), earned = starsGet(store_) / 3;
  const int e = earned < tot ? earned : tot;
  return e - store_.get("alb_open", 0);
}

void CheckIn::tick(const TileInput& in) {
  t_ += in.dt;
  clock_ += in.dt;
  idle_ += in.dt;
  epoch_ = in.epoch;
  if (chosen_ < 0) {
    if (in.tap && in.tapY > 396 && albumTile_ >= 0 && cardsWaiting() > 0) { requestGoto(albumTile_); return; }
    if (in.tap) {
      idle_ = 0;
      for (int i = 0; i < N_FEELINGS; i++) {
        float x, y;
        faceCenter(i, x, y);
        const float dx = in.tapX - x, dy = in.tapY - y;
        if (dx * dx + dy * dy < (FACE_R + 16) * (FACE_R + 16)) {
          chosen_ = i;
          t_ = 0;
          log(i, in.epoch);
          Synth::Voice v; v.wave = Synth::SINE; v.f0 = 520; v.f1 = 780; v.attack = 0.01f; v.dur = 0.25f; v.vol = 0.15f; synth_.play(v);
          break;
        }
      }
    }
  } else {
    // Antippen beschleunigt den Wechsel
    const bool go = t_ > GO_AFTER || (in.tap && t_ > 0.8f);
    if (chosen_ == FROH) {
      if (go && t_ > HAPPY_BACK - (in.tap ? 3 : 0)) { chosen_ = -1; again_ = false; t_ = 0; }
    } else if (go) {
      requestGoto(chosen_ == WUT ? grummelTile_ : breatheTile_);
      chosen_ = -1;
      again_ = false;
      t_ = 0;
    }
  }
}

// ---------------- Gesichter ----------------

void drawFace(Scene& s, int f, float x, float y, float r, float clock, float a) {
  if (f == CheckIn::KRIBBELIG) x += 2.2f * std::sin(clock * 13);   // zappelt
  const FeelingInfo& fi = FEEL[f];
  s.glow(x, y, r * 1.35f, fi.bottom, 0.22f * a, r * 0.9f);
  s.ellipse(x, y, r, r, fi.top, fi.bottom, a);
  s.ellipse(x - r * 0.34f, y - r * 0.5f, r * 0.26f, r * 0.12f, hex(0xffffff), 0.22f * a);
  const float ex = r * 0.33f, ey = y - r * 0.1f, e = r * 0.12f;
  const bool blink = std::fmod(clock + f * 0.7f, 4.3f) < 0.13f;
  switch (f) {
    case CheckIn::WUT:
      for (int sd = -1; sd <= 1; sd += 2) {
        s.circle(x + sd * ex, ey + r * 0.04f, e, INK, a);
        s.capsule(x + sd * r * 0.52f, y - r * 0.42f, x + sd * r * 0.17f, y - r * 0.27f, r * 0.055f, INK, a);   // Augenbrauen nach innen unten
      }
      s.arc(x, y + r * 0.62f, r * 0.3f, r * 0.08f, -0.85f, 0.85f, INK, a);   // Mund nach unten
      for (int k = 0; k < 2; k++) {   // kleine Dampfwölkchen
        const float u = std::fmod(clock * 0.8f + k * 0.5f, 1.f);
        s.glow(x + (k ? 1 : -1) * r * 0.55f, y - r * (1.0f + 0.5f * u), r * 0.22f, hex(0xffd0c0), 0.5f * (1 - u) * a);
      }
      break;
    case CheckIn::TRAURIG: {
      for (int sd = -1; sd <= 1; sd += 2) {
        if (blink) s.capsule(x + sd * ex - e, ey, x + sd * ex + e, ey, r * 0.04f, INK, a);
        else s.circle(x + sd * ex, ey, e, INK, a);
        s.capsule(x + sd * r * 0.5f, y - r * 0.3f, x + sd * r * 0.18f, y - r * 0.42f, r * 0.05f, INK, a);   // innen hoch
      }
      s.arc(x, y + r * 0.6f, r * 0.24f, r * 0.07f, -0.8f, 0.8f, INK, a);
      const float u = std::fmod(clock * 0.45f, 1.f);   // Träne
      s.ellipse(x + ex + r * 0.05f, ey + r * (0.2f + 0.45f * u), r * 0.06f, r * 0.09f, hex(0xd8ecff), (1 - u) * a);
      break;
    }
    case CheckIn::KRIBBELIG:
      for (int sd = -1; sd <= 1; sd += 2) {
        s.circle(x + sd * ex, ey, e * 1.35f, hex(0xffffff), a);
        s.circle(x + sd * ex + std::sin(clock * 9 + sd) * e * 0.35f, ey, e * 0.7f, INK, a);
      }
      for (int k = 0; k < 4; k++) {   // Zickzack-Mund
        const float x0 = x - r * 0.36f + k * r * 0.18f, x1 = x0 + r * 0.18f;
        s.capsule(x0, y + r * 0.42f + (k & 1 ? r * 0.08f : 0), x1, y + r * 0.42f + (k & 1 ? 0 : r * 0.08f), r * 0.04f, INK, a);
      }
      break;
    case CheckIn::MUEDE:
      for (int sd = -1; sd <= 1; sd += 2) s.arc(x + sd * ex, ey - r * 0.02f, e, r * 0.05f, PIF - 1.2f, PIF + 1.2f, INK, a);   // Augen zu
      s.ellipse(x, y + r * 0.45f, r * 0.08f, r * 0.11f, INK, a);   // kleines O
      {
        const float u = std::fmod(clock * 0.4f, 1.f);
        s.text(fonts::small, "z", x + r * 0.75f + u * 8, y - r * 0.8f - u * 14, hex(0xe8e0ff), (1 - u) * a, Align::Center);
      }
      break;
    default:   // froh
      for (int sd = -1; sd <= 1; sd += 2) s.arc(x + sd * ex, ey + e * 0.6f, e * 1.05f, r * 0.06f, -1.2f, 1.2f, INK, a);   // lachende Augen
      s.arc(x, y + r * 0.05f, r * 0.42f, r * 0.08f, PIF - 1.0f, PIF + 1.0f, INK, a);
      s.ellipse(x - r * 0.58f, y + r * 0.2f, r * 0.14f, r * 0.08f, hex(0xff8fb8), 0.5f * a);
      s.ellipse(x + r * 0.58f, y + r * 0.2f, r * 0.14f, r * 0.08f, hex(0xff8fb8), 0.5f * a);
      break;
  }
}

void CheckIn::draw(Scene& s) {
  if (chosen_ < 0) {
    // Frage oben; blendet nach einer Weile ohne Bedienung langsam weg (Einbrennschutz) und kommt wieder
    const float k = std::fmod(idle_, 40.f);
    const float qa = idle_ < 20 ? 1.f : 1 - smooth((k - 20) / 1.5f) * (1 - smooth((k - 36) / 1.5f));
    s.text(fonts::title, again_ ? "Und wie fühlt sich" : "Wie fühlt sich", W / 2, 28, TXT, qa, Align::Center);
    s.text(fonts::title, again_ ? "dein Bauch jetzt an?" : "dein Bauch an?", W / 2, 66, TXT, qa, Align::Center);
    for (int i = 0; i < N_FEELINGS; i++) {
      float x, y;
      faceCenter(i, x, y);
      y += 3 * std::sin(clock_ * 1.4f + i * 1.3f);
      drawFace(s, i, x, y, FACE_R, clock_);
      s.text(fonts::body, FEEL[i].label, x, y + FACE_R + 8, TXT_DIM, 1, Align::Center);
    }
    if (albumTile_ >= 0 && cardsWaiting() > 0) {   // Hinweis auf eine neue Pferdekarte
      const float p = 0.75f + 0.25f * std::sin(clock_ * 3);
      s.rrect(W / 2 - 112, 402, 224, 34, 17, hex(0xffd166), p);
      s.star(W / 2 - 88, 419, 9, hex(0x2a1a00), p);
      s.text(fonts::body, "Neue Pferdekarte!", W / 2 + 12, 404, hex(0x2a1a00), p, Align::Center);
    } else {
      drawStarCount(s, W / 2, 404, starsGet(store_), 0.85f);
    }
    return;
  }
  // gewähltes Gefühl rückt groß in die Mitte, die anderen verblassen
  const float u = smooth(t_ / 0.5f);
  for (int i = 0; i < N_FEELINGS; i++) {
    float x, y;
    faceCenter(i, x, y);
    if (i == chosen_) {
      x += (W / 2 - x) * u;
      y += (CHOSEN_Y - y) * u;
      drawFace(s, i, x, y, FACE_R + (CHOSEN_R - FACE_R) * u, clock_);
    } else {
      drawFace(s, i, x, y, FACE_R * (1 - 0.3f * u), clock_, 1 - u);
    }
  }
  if (chosen_ == FROH)   // Glitzer
    for (int k = 0; k < 7; k++) {
      const float an = k * 0.9f + t_ * 0.6f, rr = CHOSEN_R + 22 + 10 * std::sin(t_ * 3 + k);
      s.star(W / 2 + rr * std::sin(an), CHOSEN_Y - rr * std::cos(an), 7 + 2 * std::sin(t_ * 5 + k), hex(0xffe08a), smooth((t_ - 0.4f) / 0.4f) * 0.9f, t_);
    }
  const float ta = smooth((t_ - 0.45f) / 0.4f);
  s.text(fonts::title, FEEL[chosen_].say1, W / 2, 304, TXT, ta, Align::Center);
  char lines[2][160];
  int n = wrapText(fonts::body, FEEL[chosen_].say2, 330, lines, 2);
  if (n < 0) n = 2;
  for (int i = 0; i < n; i++) s.text(fonts::body, lines[i], W / 2, 350 + i * 30, TXT_DIM, ta, Align::Center);
}

}  // namespace dk
