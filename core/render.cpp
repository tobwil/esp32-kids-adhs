// Heiße Schleifen: hier lohnt -O2 (sonst baut das Projekt mit -Os)
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "render.h"

#include <cmath>
#include <cstring>

namespace dk {

static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
// fminf/fmaxf sind auf dem ESP32 Bibliotheksaufrufe
static inline float fmn(float a, float b) { return a < b ? a : b; }
static inline float fmx(float a, float b) { return a > b ? a : b; }

Col mix(Col a, Col b, float t) {
  t = clampf(t, 0, 1);
  return {uint8_t(a.r + (b.r - a.r) * t + 0.5f), uint8_t(a.g + (b.g - a.g) * t + 0.5f), uint8_t(a.b + (b.b - a.b) * t + 0.5f)};
}
Col scale(Col c, float f) {
  return {uint8_t(clampi(int(c.r * f + 0.5f), 0, 255)), uint8_t(clampi(int(c.g * f + 0.5f), 0, 255)), uint8_t(clampi(int(c.b * f + 0.5f), 0, 255))};
}

// RGB565 → 0x07E0F81F-Form (Grün oben, Rot und Blau unten), damit drei Kanäle in einer Rechnung laufen
static inline uint32_t spread(uint32_t c) { return (c | (c << 16)) & 0x07E0F81Fu; }

// a: 0..256, intern 33 Stufen (reicht für Kantenglättung, eine Multiplikation für alle Kanäle)
static inline uint16_t blend565(uint16_t d, uint16_t s, int a) {
  if (a >= 252) return s;
  const uint32_t a5 = uint32_t(a + 4) >> 3;
  if (!a5) return d;
  const uint32_t bg = spread(d), fg = spread(s);
  const uint32_t r = ((((fg - bg) * a5) >> 5) + bg) & 0x07E0F81Fu;
  return uint16_t(r | (r >> 16));
}
// Deckkraft 0..1 → 0..256
static inline int cov256(float c) { return c >= 1 ? 256 : c <= 0 ? 0 : int(c * 256); }

// atan2 genähert (Fehler < 0,01 rad), reicht für Kantenglättung und Bogengrenzen
static inline float fatan2(float y, float x) {
  float ax = std::fabs(x), ay = std::fabs(y);
  float mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
  float a = mn / (mx + 1e-9f), s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = PIF - r;
  if (y < 0) r = -r;
  return r;
}

// Wurzel ohne Bibliotheksaufruf (auf dem ESP32 deutlich schneller): Schätzung + zwei Newton-Schritte, Fehler < 1e-5
static inline float fsqrt(float x) {
  if (x <= 0) return 0;
  union { float f; uint32_t i; } u{x};
  u.i = 0x5f3759df - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - 0.5f * x * y * y);
  y = y * (1.5f - 0.5f * x * y * y);
  return x * y;
}

// floor/ceil ohne Bibliotheksaufruf (dort ~70 Takte)
static inline int ffloor(float x) { const int i = int(x); return i - (x < float(i)); }
static inline int fceil(float x) { const int i = int(x); return i + (x > float(i)); }

// UTF-8 → Unicode (1 bis 3 Byte)
static uint32_t nextCp(const char*& s) {
  uint8_t c = uint8_t(*s++);
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *s) return ((c & 0x1F) << 6) | (uint8_t(*s++) & 0x3F);
  if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
    uint32_t cp = ((c & 0x0F) << 12) | ((uint8_t(s[0]) & 0x3F) << 6) | (uint8_t(s[1]) & 0x3F);
    s += 2;
    return cp;
  }
  while ((uint8_t(*s) & 0xC0) == 0x80) s++;
  return '?';
}
static const fonts::Glyph* findGlyph(const fonts::Font& f, uint32_t cp) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (f.glyphs[m].cp == cp) return &f.glyphs[m];
    if (f.glyphs[m].cp < cp) lo = m + 1; else hi = m - 1;
  }
  return cp == '?' ? nullptr : findGlyph(f, '?');
}

uint32_t (*Scene::timer)() = nullptr;

const char* Scene::kindName(int k) {
  static const char* const n[9] = {"ellipse", "blob", "glow", "bogen", "kapsel", "rechteck", "text", "stern", "bild"};
  return n[k];
}

Scene::Scene() {
  cmds_.reserve(512);
  tables_.reserve(64 * 16);
  luts_.reserve(129 * 64);
  chars_.reserve(2048);
}

void Scene::reset() {
  cmds_.clear();
  tables_.clear();
  luts_.clear();
  chars_.clear();
  ox = oy = 0;
  fade = 1;
}

bool Scene::push(Cmd& c, float ymin, float ymax, float a) {
  a *= fade;
  if (a <= 0.004f) return false;
  int y0 = clampi(int(std::floor(ymin)), 0, H), y1 = clampi(int(std::ceil(ymax)) + 1, 0, H);
  if (y0 >= y1) return false;
  c.alpha = uint8_t(clampi(int(a * 255 + 0.5f), 0, 255));
  c.y0 = int16_t(y0);
  c.y1 = int16_t(y1);
  cmds_.push_back(c);
  return true;
}

void Scene::circle(float x, float y, float r, Col c, float a) { ellipse(x, y, r, r, c, c, a); }

void Scene::ellipse(float x, float y, float rx, float ry, Col top, Col bottom, float a) {
  if (rx <= 0 || ry <= 0) return;
  Cmd c{};
  c.kind = ELLIPSE; c.c0 = top; c.c1 = bottom;
  x += ox; y += oy;
  c.p[0] = x; c.p[1] = y; c.p[2] = rx; c.p[3] = ry;
  push(c, y - ry - 1, y + ry + 1, a);
}

void Scene::blob(float x, float y, float rx, float ry, const float wob[6], Col top, Col bottom, float a) {
  if (rx <= 0 || ry <= 0) return;
  Cmd c{};
  c.kind = BLOB; c.c0 = top; c.c1 = bottom;
  x += ox; y += oy;
  // Randradius je Winkel vorab berechnen (64 Stützstellen, Winkel wie atan2: -π..π)
  c.aux = int(tables_.size());
  float rmin = 1e9f, rmax = 0;
  for (int i = 0; i < 64; i++) {
    float th = -PIF + TAU * i / 64.f;
    float r = 1 + wob[0] * std::sin(2 * th + wob[1]) + wob[2] * std::sin(3 * th + wob[3]) + wob[4] * std::sin(4 * th + wob[5]);
    tables_.push_back(r);
    rmin = r < rmin ? r : rmin;
    rmax = r > rmax ? r : rmax;
  }
  c.p[0] = x; c.p[1] = y; c.p[2] = rx; c.p[3] = ry; c.p[4] = rmin; c.p[5] = rmax;
  if (!push(c, y - ry * rmax - 1, y + ry * rmax + 1, a)) tables_.resize(c.aux);
}

void Scene::glow(float x, float y, float r, Col col, float a, float hole) {
  if (r <= 0) return;
  Cmd c{};
  c.kind = GLOW; c.c0 = col;
  x += ox; y += oy;
  c.p[0] = x; c.p[1] = y; c.p[2] = r; c.p[3] = hole;
  c.aux = int(luts_.size());
  if (!push(c, y - r, y + r, a)) return;
  // Farbe je Abstandsstufe (d²/r² in 128 Stufen) vorab, Abfall (1 - d²/r²)²
  const float A = cmds_.back().alpha / 255.f;
  for (int i = 0; i <= 128; i++) {
    const float w = 1 - i / 128.f, f = A * w * w;
    luts_.push_back(spread(to565({uint8_t(col.r * f + 0.5f), uint8_t(col.g * f + 0.5f), uint8_t(col.b * f + 0.5f)})));
  }
}

void Scene::arc(float x, float y, float r, float thick, float a0, float a1, Col col, float a) {
  if (thick <= 0 || a1 <= a0) return;
  Cmd c{};
  c.kind = ARC; c.c0 = col;
  x += ox; y += oy;
  const float ht = thick * 0.5f, spanA = a1 - a0 < TAU ? a1 - a0 : TAU;
  a0 = std::fmod(a0, TAU);
  if (a0 < 0) a0 += TAU;
  c.p[0] = x; c.p[1] = y; c.p[2] = r; c.p[3] = ht; c.p[4] = a0; c.p[5] = spanA;
  push(c, y - r - ht - 1, y + r + ht + 1, a);
}

void Scene::capsule(float x0, float y0, float x1, float y1, float rad, Col col, float a) {
  if (rad <= 0) return;
  Cmd c{};
  c.kind = CAPSULE; c.c0 = col;
  x0 += ox; x1 += ox; y0 += oy; y1 += oy;
  c.p[0] = x0; c.p[1] = y0; c.p[2] = x1; c.p[3] = y1; c.p[4] = rad;
  push(c, (y0 < y1 ? y0 : y1) - rad - 1, (y0 > y1 ? y0 : y1) + rad + 1, a);
}

void Scene::rrect(float x, float y, float w, float h, float rad, Col col, float a) {
  if (w <= 0 || h <= 0) return;
  Cmd c{};
  c.kind = RRECT; c.c0 = col;
  x += ox; y += oy;
  rad = clampf(rad, 0, (w < h ? w : h) * 0.5f);
  c.p[0] = x; c.p[1] = y; c.p[2] = w; c.p[3] = h; c.p[4] = rad;
  push(c, y - 1, y + h + 1, a);
}

void Scene::image(const Image& img, float x, float y, float rad, float a) {
  if (!img.px || img.w <= 0 || img.h <= 0) return;
  Cmd c{};
  c.kind = IMAGE;
  c.pix = img.px;
  x = std::round(x + ox); y = std::round(y + oy);
  c.p[0] = x; c.p[1] = y; c.p[2] = float(img.w); c.p[3] = float(img.h); c.p[4] = rad;
  push(c, y, y + img.h - 1, a);
}

void Scene::star(float x, float y, float r, Col col, float a, float rot) {
  if (r <= 0) return;
  Cmd c{};
  c.kind = STAR; c.c0 = col;
  x += ox; y += oy;
  // zehn Eckpunkte abwechselnd außen und innen, in tables_ abgelegt
  c.aux = int(tables_.size());
  for (int i = 0; i < 10; i++) {
    const float an = rot + i * (PIF / 5), rr = (i & 1) ? r * 0.45f : r;
    tables_.push_back(x + rr * std::sin(an));
    tables_.push_back(y - rr * std::cos(an));
  }
  c.p[0] = x; c.p[1] = y; c.p[2] = r;
  if (!push(c, y - r - 1, y + r + 1, a)) tables_.resize(c.aux);
}

int Scene::textW(const fonts::Font& f, const char* s) {
  int w = 0;
  while (*s) {
    const fonts::Glyph* g = findGlyph(f, nextCp(s));
    if (g) w += g->adv;
  }
  return w;
}

void Scene::text(const fonts::Font& f, const char* s, float x, float y, Col col, float a, Align al) {
  if (!s || !*s) return;
  Cmd c{};
  c.kind = TEXT; c.c0 = col; c.font = &f;
  if (al != Align::Left) x -= al == Align::Center ? textW(f, s) * 0.5f : float(textW(f, s));
  x += ox; y += oy;
  c.p[0] = std::round(x); c.p[1] = std::round(y);
  c.aux = int(chars_.size());
  size_t n = std::strlen(s);
  chars_.insert(chars_.end(), s, s + n + 1);
  if (!push(c, c.p[1], c.p[1] + f.lineH, a)) chars_.resize(c.aux);
}

// ---------------- Rastern ----------------

void Scene::rasterize(uint16_t* buf, int y0, int rows, bool bigEndian) const {
  std::memset(buf, 0, size_t(W) * rows * 2);
  const int y1 = y0 + rows;
  for (const Cmd& c : cmds_) {
    if (c.y1 <= y0 || c.y0 >= y1) continue;
    int a = c.y0 > y0 ? c.y0 : y0, b = c.y1 < y1 ? c.y1 : y1;
    uint16_t* base = buf - y0 * W;   // base + y * W = Zeile y
    const uint32_t t0 = timer ? timer() : 0;
    switch (c.kind) {
      case ELLIPSE: drawEllipse(c, base, a, b); break;
      case BLOB: drawBlob(c, base, a, b); break;
      case GLOW: drawGlow(c, base, a, b); break;
      case ARC: drawArc(c, base, a, b); break;
      case CAPSULE: drawCapsule(c, base, a, b); break;
      case RRECT: drawRRect(c, base, a, b); break;
      case TEXT: drawText(c, base, a, b); break;
      case STAR: drawStar(c, base, a, b); break;
      case IMAGE: drawImage(c, base, a, b); break;
    }
    if (timer) kindUs[c.kind] += timer() - t0;
  }
  if (bigEndian) {   // je zwei Pixel auf einmal, schwarze überspringen
    uint32_t* p = reinterpret_cast<uint32_t*>(buf);
    for (int i = 0, n = W * rows / 2; i < n; i++) {
      const uint32_t v = p[i];
      if (v) p[i] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    }
  }
}

// Zeilenbereich [xa, xb] (Pixelmitten) innerhalb der halben Breite hx um cx
static inline void span(float cx, float hx, int& xa, int& xb) {
  xa = clampi(fceil(cx - hx - 0.5f), 0, W);
  xb = clampi(ffloor(cx + hx - 0.5f), -1, W - 1);
}

void Scene::drawEllipse(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float cx = c.p[0], cy = c.p[1], rx = c.p[2], ry = c.p[3];
  const int A = c.alpha + (c.alpha >> 7);
  const float irx2 = 1.f / (rx * rx), iry2 = 1.f / (ry * ry);
  const bool circ = rx == ry;
  const float r2 = rx * rx, i2r = 0.5f / rx;
  const bool grad = c.c0.r != c.c1.r || c.c0.g != c.c1.g || c.c0.b != c.c1.b;
  uint16_t col = to565(c.c0);
  const float ryo = ry + 1, rxo = rx + 1, iryo2 = 1.f / (ryo * ryo), iryi2 = ry > 1.5f ? 1.f / ((ry - 1) * (ry - 1)) : 0, i2ry = 0.5f / ry;
  for (int y = y0; y < y1; y++) {
    const float dy = y + 0.5f - cy, ady = std::fabs(dy);
    if (ady >= ryo) continue;
    const float hxo = rxo * fsqrt(1 - dy * dy * iryo2);
    float hxi = -1;
    if (rx > 1.5f && ry > 1.5f && ady < ry - 1) hxi = (rx - 1) * fsqrt(1 - dy * dy * iryi2);
    if (grad) col = to565(mix(c.c0, c.c1, (dy + ry) * i2ry));
    int xa, xb, ia = 1, ib = 0;
    span(cx, hxo, xa, xb);
    if (hxi > 0) span(cx, hxi, ia, ib);
    uint16_t* row = fb + y * W;
    for (int x = xa; x <= xb; x++) {
      if (x >= ia && x <= ib) {
        if (A >= 256) {   // Innenbereich in einem Rutsch füllen
          for (; x <= ib; x++) row[x] = col;
        } else {
          for (; x <= ib; x++) row[x] = blend565(row[x], col, A);
        }
        x--;
        continue;
      }
      const float dx = x + 0.5f - cx;
      float d;
      if (circ) d = (dx * dx + dy * dy - r2) * i2r;   // Kreis: d - r ≈ (d² - r²) / 2r
      else {
        const float F = dx * dx * irx2 + dy * dy * iry2 - 1;
        const float gx = dx * irx2, gy = dy * iry2;
        d = F / (2 * fsqrt(gx * gx + gy * gy) + 1e-6f);
      }
      const int k = cov256(0.5f - d);
      if (k > 0) row[x] = blend565(row[x], col, (A * k) >> 8);
    }
  }
}

void Scene::drawBlob(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float cx = c.p[0], cy = c.p[1], rx = c.p[2], ry = c.p[3], rmin = c.p[4], rmax = c.p[5];
  const float* tab = &tables_[c.aux];
  const int A = c.alpha + (c.alpha >> 7);
  const float s = (rx + ry) * 0.5f, irx = 1.f / rx, iry = 1.f / ry;
  const float rxo = rx * rmax + 1, ryo = ry * rmax + 1, rxi = rx * rmin - 1, ryi = ry * rmin - 1;
  const float iryo2 = 1.f / (ryo * ryo), iryi2 = ryi > 0 ? 1.f / (ryi * ryi) : 0, i2ry = 0.5f / ry;
  for (int y = y0; y < y1; y++) {
    const float dy = y + 0.5f - cy, ady = std::fabs(dy);
    if (ady >= ryo) continue;
    const float hxo = rxo * fsqrt(1 - dy * dy * iryo2);
    float hxi = -1;
    if (rxi > 0 && ryi > 0 && ady < ryi) hxi = rxi * fsqrt(1 - dy * dy * iryi2);
    const uint16_t col = to565(mix(c.c0, c.c1, (dy + ry) * i2ry));
    int xa, xb, ia = 1, ib = 0;
    span(cx, hxo, xa, xb);
    if (hxi > 0) span(cx, hxi, ia, ib);
    uint16_t* row = fb + y * W;
    const float qy = dy * iry;
    for (int x = xa; x <= xb; x++) {
      if (x >= ia && x <= ib) {
        if (A >= 256) for (; x <= ib; x++) row[x] = col;
        else for (; x <= ib; x++) row[x] = blend565(row[x], col, A);
        x--;
        continue;
      }
      const float qx = (x + 0.5f - cx) * irx;
      const float rho = fsqrt(qx * qx + qy * qy);
      float t = (fatan2(qy, qx) + PIF) * (64.f / TAU);
      int i = int(t);
      float f = t - i;
      i &= 63;
      const float r = tab[i] + (tab[(i + 1) & 63] - tab[i]) * f;
      const int k = cov256(0.5f - (rho - r) * s);
      if (k > 0) row[x] = blend565(row[x], col, (A * k) >> 8);
    }
  }
}

void Scene::drawGlow(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float cx = c.p[0], cy = c.p[1], r = c.p[2], hole = c.p[3];
  const uint32_t* lut = &luts_[c.aux];
  // Rand weglassen, wo die Tabelle schon schwarz ist
  int last = 128;
  while (last > 0 && lut[last - 1] == 0) last--;
  if (!last) return;
  const float re2 = r * r * (last / 128.f);
  // Stufe = d²/r² · 128 in 16.16-Festkomma, entlang der Zeile per Differenzen (quadratisch in x)
  const float k = 128.f * 65536.f / (r * r);
  const int ddq = int(2 * k);
  for (int y = y0; y < y1; y++) {
    const float dyf = y + 0.5f - cy;
    if (dyf * dyf >= re2) continue;
    int xa, xb, ha = 1, hb = 0;
    span(cx, fsqrt(re2 - dyf * dyf), xa, xb);
    if (xa > xb) continue;
    if (hole > 0 && std::fabs(dyf) < hole) span(cx, fsqrt(hole * hole - dyf * dyf), ha, hb);
    uint16_t* row = fb + y * W;
    // links und rechts vom Loch (ohne Loch: ein Abschnitt)
    for (int part = 0; part < 2; part++) {
      int s0 = xa, s1 = xb;
      if (hb >= ha) { if (part == 0) s1 = ha - 1; else s0 = hb + 1; }
      else if (part) break;
      if (s0 > s1) continue;
      const float dx = s0 + 0.5f - cx;
      int q = int((dx * dx + dyf * dyf) * k), dq = int((2 * dx + 1) * k);
      for (int x = s0; x <= s1; x++, q += dq, dq += ddq) {
        const int idx = q >> 16;
        if (unsigned(idx) >= unsigned(last)) continue;
        // gesättigte Addition aller drei Kanäle auf einmal
        uint32_t sum = spread(row[x]) + lut[idx];
        const uint32_t ov = sum & 0x08010020u;   // Überlauf von Grün, Rot, Blau
        sum |= ov - (ov >> 5);
        sum &= 0x07E0F81Fu;
        row[x] = uint16_t(sum | (sum >> 16));
      }
    }
  }
}

// Bogen: pro Zeile werden die Bereiche voller Deckung analytisch bestimmt und direkt gefüllt,
// nur die Kantenpixel (je ~1 px an Innen- und Außenrand) einzeln gerechnet. Der Winkelbereich ist pro Zeile
// ein Intervall (Spanne ≤ π) oder das Komplement davon (Spanne > π), die runden Kappen kommen danach.
namespace {
struct Iv { float lo, hi; };   // dx-Intervall, geschlossen
// Halbebene a + b·dx ≥ 0 als Intervall
inline Iv halfPlane(float a, float b) {
  if (std::fabs(b) < 1e-6f) return a >= 0 ? Iv{-1e9f, 1e9f} : Iv{1, 0};
  const float t = -a / b;
  return b > 0 ? Iv{t, 1e9f} : Iv{-1e9f, t};
}
}  // namespace

void Scene::drawArc(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float cx = c.p[0], cy = c.p[1], r = c.p[2], ht = c.p[3], a0 = c.p[4], spanA = c.p[5];
  const bool full = spanA >= TAU - 1e-4f, big = spanA > PIF;
  const int A = c.alpha + (c.alpha >> 7);
  const uint16_t col = to565(c.c0);
  const float a1 = a0 + spanA;
  const float u0x = std::sin(a0), u0y = -std::cos(a0), u1x = std::sin(a1), u1y = -std::cos(a1);
  const float e0x = cx + r * u0x, e0y = cy + r * u0y, e1x = cx + r * u1x, e1y = cy + r * u1y;
  // Kantenzonen: je 1 px um Außen- und Innenrand
  const float Ro = r + ht + 0.5f, Ri = r - ht - 0.5f;
  const bool thin = ht < 1.f;   // keine Zone voller Deckung
  const float Rfo = r + ht - 0.5f, Rfi = r - ht + 0.5f;
  // Deckung an den Rändern ohne Wurzel: d - R ≈ (d² - R²) / 2R
  const float Rout = r + ht, Rin = r - ht > 0.5f ? r - ht : 0.5f;
  const float iOut = 0.5f / Rout, iIn = 0.5f / Rin, Rout2 = Rout * Rout, Rin2 = Rin * Rin;

  auto half = [](float R, float dy) { const float q = R * R - dy * dy; return R > 0 && q > 0 ? fsqrt(q) : -1.f; };
  auto pix = [&](uint16_t* row, int x, float cov) {
    const int k = cov256(cov);
    if (k > 0) row[x] = blend565(row[x], col, (A * k) >> 8);
  };

  for (int y = y0; y < y1; y++) {
    const float dy = y + 0.5f - cy;
    const float hO = half(Ro, dy);
    if (hO < 0) continue;
    uint16_t* row = fb + y * W;
    // Winkel-Intervalle dieser Zeile (in dx)
    Iv ang[2];
    int nAng = 0;
    if (full) ang[nAng++] = {-1e9f, 1e9f};
    else {
      const Iv s0 = halfPlane(u0x * dy, -u0y), s1 = halfPlane(-(u1x * dy), u1y);   // c0 ≥ 0, c1 ≤ 0
      if (!big) {
        const Iv i{fmx(s0.lo, s1.lo), fmn(s0.hi, s1.hi)};
        if (i.lo <= i.hi) ang[nAng++] = i;
      } else {
        if (s0.lo <= s0.hi) ang[nAng++] = s0;
        if (s1.lo <= s1.hi) ang[nAng++] = s1;
      }
    }
    if (!nAng) continue;
    // radiale Abschnitte: Kanten (per Pixel) und volle Deckung
    struct Seg { float lo, hi; bool edge; };
    Seg seg[7];
    int ns = 0;
    const float hFO = thin ? -1 : half(Rfo, dy), hFI = thin ? -1 : half(Rfi, dy), hI = half(Ri, dy);
    if (hFO < 0) {
      if (hI < 0) seg[ns++] = {-hO, hO, true};
      else { seg[ns++] = {-hO, -hI, true}; seg[ns++] = {hI, hO, true}; }
    } else {
      seg[ns++] = {-hO, -hFO, true};
      if (hFI < 0) seg[ns++] = {-hFO, hFO, false};
      else {
        seg[ns++] = {-hFO, -hFI, false};
        if (hI < 0) seg[ns++] = {-hFI, hFI, true};
        else { seg[ns++] = {-hFI, -hI, true}; seg[ns++] = {hI, hFI, true}; }
        seg[ns++] = {hFI, hFO, false};
      }
      seg[ns++] = {hFO, hO, true};
    }
    for (int a = 0; a < nAng; a++) {
      for (int i = 0; i < ns; i++) {
        const float lo = fmx(seg[i].lo, ang[a].lo), hi = fmn(seg[i].hi, ang[a].hi);
        if (lo > hi) continue;
        const int xa = clampi(fceil(cx + lo - 0.5f), 0, W), xb = clampi(ffloor(cx + hi - 0.5f), -1, W - 1);
        if (!seg[i].edge) {
          if (A >= 252) for (int x = xa; x <= xb; x++) row[x] = col;
          else for (int x = xa; x <= xb; x++) row[x] = blend565(row[x], col, A);
        } else {
          float dx = xa + 0.5f - cx;
          for (int x = xa; x <= xb; x++, dx += 1) {
            const float d2 = dx * dx + dy * dy;
            const float cov = fmn(0.5f - (d2 - Rout2) * iOut, 0.5f + (d2 - Rin2) * iIn);
            pix(row, x, thin ? fmn(cov, 2 * ht) : cov);
          }
        }
      }
    }
    // runde Kappen: nur Pixel außerhalb des Winkelbereichs
    if (!full) {
      const float capR = ht + 0.5f;
      for (int e = 0; e < 2; e++) {
        const float ex = e ? e1x : e0x, ey = e ? e1y : e0y;
        if (std::fabs(y + 0.5f - ey) >= capR) continue;
        const int xa = clampi(int(ex - capR), 0, W - 1), xb = clampi(int(ex + capR) + 1, 0, W - 1);
        for (int x = xa; x <= xb; x++) {
          const float dx = x + 0.5f - cx;
          const float c0 = u0x * dy - u0y * dx, c1 = u1x * dy - u1y * dx;
          const bool inside = big ? (c0 >= 0 || c1 <= 0) : (c0 >= 0 && c1 <= 0);
          if (inside) continue;
          const float px = x + 0.5f - ex, py = y + 0.5f - ey;
          pix(row, x, capR - fsqrt(px * px + py * py));
        }
      }
    }
  }
}

void Scene::drawCapsule(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float ax = c.p[0], ay = c.p[1], bx = c.p[2], by = c.p[3], rad = c.p[4];
  const float vx = bx - ax, vy = by - ay, il = 1.f / (vx * vx + vy * vy + 1e-6f);
  const float ro2 = (rad + 0.5f) * (rad + 0.5f), ri2 = rad > 0.5f ? (rad - 0.5f) * (rad - 0.5f) : -1;
  const int A = c.alpha + (c.alpha >> 7);
  const uint16_t col = to565(c.c0);
  const int xa = clampi(int(std::floor((ax < bx ? ax : bx) - rad - 1)), 0, W - 1);
  const int xb = clampi(int(std::ceil((ax > bx ? ax : bx) + rad + 1)), 0, W - 1);
  for (int y = y0; y < y1; y++) {
    uint16_t* row = fb + y * W;
    const float py = y + 0.5f - ay;
    for (int x = xa; x <= xb; x++) {
      const float px = x + 0.5f - ax;
      const float t = clampf((px * vx + py * vy) * il, 0, 1);
      const float dx = px - vx * t, dy = py - vy * t, d2 = dx * dx + dy * dy;
      if (d2 >= ro2) continue;                                            // sicher außen
      if (d2 <= ri2) { row[x] = A >= 252 ? col : blend565(row[x], col, A); continue; }   // sicher innen
      const float cov = rad + 0.5f - fsqrt(d2);
      if (cov <= 0) continue;
      row[x] = blend565(row[x], col, (A * cov256(cov)) >> 8);
    }
  }
}

void Scene::drawRRect(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float x0 = c.p[0], ry0 = c.p[1], w = c.p[2], h = c.p[3], rad = c.p[4];
  const float cx = x0 + w * 0.5f, cy = ry0 + h * 0.5f, hx = w * 0.5f - rad, hy = h * 0.5f - rad;
  const int A = c.alpha + (c.alpha >> 7);
  const uint16_t col = to565(c.c0);
  const float ro = rad + 0.5f, ri = rad - 0.5f;
  for (int y = y0; y < y1; y++) {
    uint16_t* row = fb + y * W;
    const float qy = std::fabs(y + 0.5f - cy) - hy;   // > 0: Zeile liegt in den Ecken
    const float dy = qy > 0 ? qy : 0;
    if (dy >= ro) continue;
    // äußere Grenze (Deckung > 0) und innere (volle Deckung) als halbe Breiten
    const float ho = hx + fsqrt(ro * ro - dy * dy);
    float hi = -1;
    if (ri > 0 && dy < ri) hi = hx + fsqrt(ri * ri - dy * dy);
    else if (rad < 0.5f && qy < -0.5f) hi = hx + rad - 0.5f;
    int xa, xb, ia = 1, ib = 0;
    span(cx, ho, xa, xb);
    if (hi > 0) span(cx, hi, ia, ib);
    for (int x = xa; x <= xb; x++) {
      if (x >= ia && x <= ib) {   // Innenbereich in einem Rutsch
        if (A >= 252) for (; x <= ib; x++) row[x] = col;
        else for (; x <= ib; x++) row[x] = blend565(row[x], col, A);
        x--;
        continue;
      }
      const float qx = std::fabs(x + 0.5f - cx) - hx;
      float d;
      if (qx <= 0 && qy <= 0) d = (qx > qy ? qx : qy) - rad;
      else {
        const float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
        d = fsqrt(mx * mx + my * my) - rad;
      }
      const int k = cov256(0.5f - d);
      if (k > 0) row[x] = blend565(row[x], col, (A * k) >> 8);
    }
  }
}

void Scene::drawText(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const fonts::Font& f = *c.font;
  const int A = c.alpha + (c.alpha >> 7);
  const uint16_t col = to565(c.c0);
  int pen = int(c.p[0]);
  const int base = int(c.p[1]) + f.ascent;
  const char* s = &chars_[c.aux];
  while (*s) {
    const fonts::Glyph* g = findGlyph(f, nextCp(s));
    if (!g) continue;
    const int gx = pen + g->ox, gy = base - g->oy;
    const int rb = (g->w + 1) / 2;
    const int ya = gy > y0 ? gy : y0, yb = gy + g->h < y1 ? gy + g->h : y1;
    for (int y = ya; y < yb; y++) {
      const uint8_t* src = f.bits + g->off + (y - gy) * rb;
      uint16_t* row = fb + y * W;
      for (int i = 0; i < g->w; i++) {
        const int x = gx + i;
        if (unsigned(x) >= unsigned(W)) continue;
        const int v = (i & 1) ? (src[i >> 1] & 15) : (src[i >> 1] >> 4);
        if (v) row[x] = blend565(row[x], col, (A * (v * 17 + (v >> 3))) >> 8);
      }
    }
    pen += g->adv;
  }
}

// Bild: Zeile für Zeile kopieren, an den runden Ecken eingerückt (Kantenpixel geglättet)
void Scene::drawImage(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const int ix = int(c.p[0]), iy = int(c.p[1]), w = int(c.p[2]), h = int(c.p[3]);
  const float rad = c.p[4];
  const int A = c.alpha + (c.alpha >> 7);
  for (int y = y0; y < y1; y++) {
    const int sy = y - iy;
    if (sy < 0 || sy >= h) continue;
    // Einrückung an den Ecken
    float inset = 0;
    const float dyc = sy + 0.5f < rad ? rad - (sy + 0.5f) : (sy + 0.5f > h - rad ? sy + 0.5f - (h - rad) : 0);
    if (dyc > 0) inset = rad - fsqrt(rad * rad - dyc * dyc);
    const int xa = clampi(ix + int(inset), 0, W), xb = clampi(ix + w - int(inset), 0, W);   // [xa, xb)
    const uint16_t* src = c.pix + sy * w - ix;
    uint16_t* row = fb + y * W;
    if (A >= 252) {
      std::memcpy(row + xa, src + xa, size_t(xb > xa ? xb - xa : 0) * 2);
    } else {
      for (int x = xa; x < xb; x++) row[x] = blend565(row[x], src[x], A);
    }
    // halbe Kantenpixel an den Ecken
    if (inset > 0) {
      const float fr = inset - int(inset);
      const int k = int((1 - fr) * A);
      if (ix + int(inset) - 1 >= 0 && ix + int(inset) - 1 < W) { const int x = ix + int(inset) - 1; row[x] = blend565(row[x], src[x], k); }
      if (ix + w - int(inset) >= 0 && ix + w - int(inset) < W) { const int x = ix + w - int(inset); if (x < ix + w) row[x] = blend565(row[x], src[x], k); }
    }
  }
}

// Stern: pro Zeile vier Unterzeilen, je Unterzeile die Schnittpunkte mit den zehn Kanten (sortiert, paarweise = innen).
// Waagerecht wird die Überdeckung jedes Pixels exakt aufsummiert, senkrecht über die Unterzeilen gemittelt.
void Scene::drawStar(const Cmd& c, uint16_t* fb, int y0, int y1) const {
  const float* v = &tables_[c.aux];
  const float cx = c.p[0], r = c.p[2];
  const int A = c.alpha + (c.alpha >> 7);
  const uint16_t col = to565(c.c0);
  const int xa = clampi(int(cx - r - 1), 0, W - 1), xb = clampi(int(cx + r + 1), 0, W - 1);
  if (xb - xa + 1 > 256) return;   // nur für Symbole
  float inv[10];
  for (int i = 0, j = 9; i < 10; j = i++) {
    const float dy = v[2 * i + 1] - v[2 * j + 1];
    inv[i] = std::fabs(dy) > 1e-6f ? (v[2 * i] - v[2 * j]) / dy : 0;
  }
  int16_t cov[257];
  for (int y = y0; y < y1; y++) {
    std::memset(cov, 0, sizeof(int16_t) * (xb - xa + 2));
    bool any = false;
    for (int sub = 0; sub < 4; sub++) {
      const float sy = y + (sub + 0.5f) * 0.25f;
      float xs[10];
      int n = 0;
      for (int i = 0, j = 9; i < 10; j = i++) {
        const float ay = v[2 * j + 1], by = v[2 * i + 1];
        if ((ay > sy) != (by > sy)) {
          float x = v[2 * j] + (sy - ay) * inv[i];
          int k = n++;   // einsortieren
          while (k > 0 && xs[k - 1] > x) { xs[k] = xs[k - 1]; k--; }
          xs[k] = x;
        }
      }
      for (int k = 0; k + 1 < n; k += 2) {
        const float l = xs[k] - xa, rr = xs[k + 1] - xa;   // relativ zu xa
        if (rr <= l) continue;
        any = true;
        const int il = l < 0 ? 0 : int(l), ir = int(rr) > xb - xa ? xb - xa : int(rr);
        for (int px = il; px <= ir; px++) {
          const float o0 = px > l ? float(px) : l, o1 = px + 1 < rr ? float(px + 1) : rr;
          if (o1 > o0) cov[px] = int16_t(cov[px] + int((o1 - o0) * 64));   // 4 Unterzeilen × 64 = 256
        }
      }
    }
    if (!any) continue;
    uint16_t* row = fb + y * W;
    for (int px = 0; px <= xb - xa; px++) {
      const int k = cov[px] > 256 ? 256 : cov[px];
      if (k > 0) row[xa + px] = blend565(row[xa + px], col, (A * k) >> 8);
    }
  }
}

}  // namespace dk
