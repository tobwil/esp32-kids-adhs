// Kleiner Vektor-Renderer für das AMOLED (368×448, RGB565), plattformunabhängig.
// Die Kacheln füllen pro Frame eine Zeichenliste (Scene); gerastert wird bandweise, damit auf dem ESP32
// nur ein paar Zeilen im schnellen internen RAM liegen und der Display-Task parallel das vorige Band schiebt.
// Alle Formen sind kantengeglättet, Koordinaten in Pixeln (float), Winkel im Bogenmaß: 0 = oben, im Uhrzeigersinn.
#pragma once
#include <cstdint>
#include <vector>

#include "font.h"

namespace dk {

constexpr int W = 368, H = 448;
constexpr float PIF = 3.14159265f, TAU = 6.2831853f;

struct Col { uint8_t r, g, b; };
constexpr Col hex(uint32_t v) { return {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }
inline uint16_t to565(Col c) { return uint16_t(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3)); }
Col mix(Col a, Col b, float t);
Col scale(Col c, float f);

enum class Align : uint8_t { Left, Center, Right };

// Bild in RGB565 (Byte-Reihenfolge der Plattform); der Speicher muss bis nach dem Rastern gültig bleiben
struct Image { const uint16_t* px = nullptr; int w = 0, h = 0; };

class Scene {
 public:
  Scene();
  void reset();

  // Wird beim Eintragen auf alle Formen angewendet: Verschiebung (Wischen, Einbrennschutz) und Deckkraft (Überblenden)
  float ox = 0, oy = 0, fade = 1;

  void circle(float x, float y, float r, Col c, float a = 1);
  // Ellipse mit senkrechtem Farbverlauf von oben (top) nach unten (bottom)
  void ellipse(float x, float y, float rx, float ry, Col top, Col bottom, float a = 1);
  void ellipse(float x, float y, float rx, float ry, Col c, float a = 1) { ellipse(x, y, rx, ry, c, c, a); }
  // Weicher Körper: Ellipse, deren Rand mit drei Sinus-Wellen (k = 2, 3, 4) wabert. wob = {a2, p2, a3, p3, a4, p4}
  void blob(float x, float y, float rx, float ry, const float wob[6], Col top, Col bottom, float a = 1);
  // Leuchten: wird aufaddiert, fällt weich bis zum Radius r ab. hole: innen nichts rechnen (dort liegt ohnehin eine deckende Form)
  void glow(float x, float y, float r, Col c, float a = 1, float hole = 0);
  // Bogen mit runden Enden von a0 bis a1 (a1 - a0 >= TAU: voller Ring)
  void arc(float x, float y, float r, float thick, float a0, float a1, Col c, float a = 1);
  void ring(float x, float y, float r, float thick, Col c, float a = 1) { arc(x, y, r, thick, 0, TAU, c, a); }
  // Strich mit runden Enden (Radius = halbe Dicke)
  void capsule(float x0, float y0, float x1, float y1, float rad, Col c, float a = 1);
  void rrect(float x, float y, float w, float h, float rad, Col c, float a = 1);
  // Bild mit der linken oberen Ecke bei (x, y), Ecken abgerundet (Radius rad)
  void image(const Image& img, float x, float y, float rad = 0, float a = 1);
  // Fünfzackiger Stern (Spitze oben, rot = Drehung im Bogenmaß), Innenradius 0,45 r. Für kleine Symbole gedacht.
  void star(float x, float y, float r, Col c, float a = 1, float rot = 0);
  // Text, y = Oberkante der Zeile
  void text(const fonts::Font& f, const char* s, float x, float y, Col c, float a = 1, Align al = Align::Left);
  static int textW(const fonts::Font& f, const char* s);

  // Band [y0, y0 + rows) in buf (W × rows) rastern, Hintergrund schwarz. bigEndian: Bytes fürs Panel tauschen
  void rasterize(uint16_t* buf, int y0, int rows, bool bigEndian = false) const;
  int size() const { return int(cmds_.size()); }

  // Messung: Rechenzeit je Formtyp (µs), nur wenn timer gesetzt ist
  static uint32_t (*timer)();
  mutable uint32_t kindUs[9] = {};
  static const char* kindName(int k);

 private:
  enum Kind : uint8_t { ELLIPSE, BLOB, GLOW, ARC, CAPSULE, RRECT, TEXT, STAR, IMAGE };
  struct Cmd {
    Kind kind;
    uint8_t alpha;
    int16_t y0, y1;      // betroffene Zeilen (inklusive y0, exklusive y1)
    Col c0, c1;
    float p[8];
    int aux;             // BLOB: Index in tables_, GLOW: Index in luts_, TEXT: Start in chars_
    const fonts::Font* font;
    const uint16_t* pix;   // IMAGE
  };
  bool push(Cmd& c, float ymin, float ymax, float a);
  void drawEllipse(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawBlob(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawGlow(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawArc(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawCapsule(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawRRect(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawText(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawStar(const Cmd& c, uint16_t* buf, int y0, int y1) const;
  void drawImage(const Cmd& c, uint16_t* buf, int y0, int y1) const;

  std::vector<Cmd> cmds_;
  std::vector<float> tables_;   // Randradius je Winkel für Blobs, 64 Werte pro Blob
  std::vector<uint32_t> luts_;  // Farbe je Abstand für Leuchten, 129 Werte pro Leuchten (gespreiztes RGB565)
  std::vector<char> chars_;
};

}  // namespace dk
