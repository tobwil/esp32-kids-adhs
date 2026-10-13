// Kantengeglättete Bitmap-Schriften (4 Bit Deckkraft je Pixel), erzeugt von tools/mkfont.swift.
#pragma once
#include <cstdint>

namespace fonts {

struct Glyph {
  uint16_t cp;      // Unicode
  uint8_t w, h;     // Bitmap-Größe
  int8_t ox, oy;    // Versatz: links vom Stift, Oberkante über der Grundlinie
  uint8_t adv;      // Vorschub
  uint32_t off;     // Start in bits[], Zeilen auf ganze Bytes aufgefüllt, 2 Pixel je Byte (hohes Nibble links)
};

struct Font {
  const Glyph* glyphs;   // nach cp sortiert
  int n;
  const uint8_t* bits;
  uint8_t lineH, ascent;
};

}  // namespace fonts
