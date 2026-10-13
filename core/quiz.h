// Fragenpaket fürs Orakel: Text mit einer Frage pro Zeile (Format siehe tools/quiz.py), erste Zeile „#pack <name>“.
// Der Text bleibt, wo er ist (Flash oder PSRAM); get() zerlegt eine Zeile in einen Zwischenpuffer.
#pragma once
#include <cstdint>
#include <vector>

#include "font.h"

namespace dk {

struct Question {
  const char* cat;
  int diff;            // 0 leicht, 1 mittel, 2 fies
  const char* q;
  const char* a[3];
  int correct;
  const char* expl;
  const char* pic;     // optionales Bild, z. B. „zaehl:7:apfel“ (sonst "")
};

class QuizPack {
 public:
  bool load(const char* text);   // Text muss so lange leben wie das Paket
  int count() const { return int(lines_.size()); }
  const char* id() const { return id_; }
  uint32_t hash() const { return hash_; }
  bool get(int i, Question& out);   // Felder gültig bis zum nächsten get()
  int categories() const { return nCats_; }
  const char* category(int i) const { return cats_[i]; }
  int categoryOf(int i) const { return lineCat_[i]; }
  int diffOf(int i) const { return lineDiff_[i]; }

 private:
  const char* text_ = nullptr;
  std::vector<uint32_t> lines_;
  std::vector<uint8_t> lineCat_, lineDiff_;
  char cats_[12][28] = {};
  int nCats_ = 0;
  char id_[32] = "";
  uint32_t hash_ = 0;
  char scratch_[720];
};

// Zeilenumbruch an Leerzeichen; gibt die Zahl der Zeilen zurück (höchstens maxLines, sonst -1)
int wrapText(const fonts::Font& f, const char* s, int maxW, char out[][160], int maxLines);

}  // namespace dk
