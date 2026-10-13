// Die vier Schriften der Kacheln (Daten in fonts.cpp, erzeugt von tools/mkfonts.sh).
// Bitmap-Fassungen, abgeleitet von Nunito und Lilita One (SIL Open Font License 1.1, siehe licenses/OFL-1.1.txt).
#pragma once
#include "font.h"

namespace fonts {
extern const Font small;   // 18 px, leicht verstärkt (aus Nunito)
extern const Font body;    // 23 px, leicht verstärkt (aus Nunito)
extern const Font title;   // 34 px, Überschriften (aus Lilita One)
extern const Font big;     // 72 px, nur Ziffern und %:×·-+?! (aus Lilita One)
}  // namespace fonts
