#include "quiz.h"

#include <cstdio>
#include <cstring>

#include "render.h"

namespace dk {

bool QuizPack::load(const char* text) {
  text_ = text;
  lines_.clear(); lineCat_.clear(); lineDiff_.clear();
  nCats_ = 0;
  id_[0] = 0;
  hash_ = 2166136261u;
  if (!text) return false;
  const char* p = text;
  while (*p) {
    const char* e = std::strchr(p, '\n');
    const size_t n = e ? size_t(e - p) : std::strlen(p);
    for (size_t i = 0; i < n; i++) hash_ = (hash_ ^ uint8_t(p[i])) * 16777619u;   // FNV-1a über den ganzen Text
    if (n > 6 && !std::strncmp(p, "#pack ", 6)) {
      const size_t k = n - 6 < sizeof id_ - 1 ? n - 6 : sizeof id_ - 1;
      std::memcpy(id_, p + 6, k);
      id_[k] = 0;
    } else if (n > 10 && p[0] != '#') {
      // Kategorie und Schwierigkeit schon beim Laden merken (für die Filter)
      const char* bar = static_cast<const char*>(std::memchr(p, '|', n));
      if (bar && bar + 2 < p + n) {
        const size_t cl = size_t(bar - p) < sizeof cats_[0] - 1 ? size_t(bar - p) : sizeof cats_[0] - 1;
        int ci = -1;
        for (int i = 0; i < nCats_; i++)
          if (!std::strncmp(cats_[i], p, cl) && cats_[i][cl] == 0) ci = i;
        if (ci < 0 && nCats_ < 12) { std::memcpy(cats_[nCats_], p, cl); cats_[nCats_][cl] = 0; ci = nCats_++; }
        lines_.push_back(uint32_t(p - text));
        lineCat_.push_back(uint8_t(ci < 0 ? 0 : ci));
        lineDiff_.push_back(uint8_t(bar[1] >= '0' && bar[1] <= '2' ? bar[1] - '0' : 1));
      }
    }
    p += n;
    if (*p == '\n') p++;
  }
  return !lines_.empty();
}

bool QuizPack::get(int i, Question& out) {
  if (i < 0 || i >= count()) return false;
  const char* p = text_ + lines_[i];
  size_t n = 0;
  while (p[n] && p[n] != '\n' && n < sizeof scratch_ - 1) n++;
  std::memcpy(scratch_, p, n);
  scratch_[n] = 0;
  const char* f[8];
  int nf = 0;
  char* s = scratch_;
  f[nf++] = s;
  for (; *s && nf < 8; s++)
    if (*s == '|') { *s = 0; f[nf++] = s + 1; }
  if (nf < 8) return false;
  out.cat = f[0];
  out.diff = f[1][0] - '0';
  out.q = f[2];
  out.a[0] = f[3]; out.a[1] = f[4]; out.a[2] = f[5];
  out.correct = f[6][0] - '0';
  out.expl = f[7];
  return out.correct >= 0 && out.correct <= 2;
}

int wrapText(const fonts::Font& f, const char* s, int maxW, char out[][160], int maxLines) {
  int n = 0;
  char line[160] = "";
  while (*s) {
    // nächstes Wort
    const char* e = s;
    while (*e && *e != ' ') e++;
    char word[160];
    size_t wl = size_t(e - s) < sizeof word - 1 ? size_t(e - s) : sizeof word - 1;
    std::memcpy(word, s, wl);
    word[wl] = 0;
    char trial[160];
    if (line[0]) snprintf(trial, sizeof trial, "%s %s", line, word);
    else snprintf(trial, sizeof trial, "%s", word);
    if (!line[0] || Scene::textW(f, trial) <= maxW) {
      std::strcpy(line, trial);
    } else {
      if (n >= maxLines) return -1;
      std::strcpy(out[n++], line);
      std::strcpy(line, word);
    }
    s = *e ? e + 1 : e;
  }
  if (line[0]) {
    if (n >= maxLines) return -1;
    std::strcpy(out[n++], line);
  }
  return n;
}

}  // namespace dk
