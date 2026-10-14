#include "radio_player.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dk {

static constexpr float FADE_SECS = 30;   // so lange blendet der Schlaf-Timer aus

void PcmRing::write(const int16_t* s, int n) {
  uint32_t w = w_.load();
  for (int i = 0; i < n; i++) buf_[(w + i) & (N - 1)] = s[i];
  w_.store(w + n);
}

int PcmRing::read(int16_t* out, int n) {
  const uint32_t r = r_.load(), avail = w_.load() - r;
  if (int(avail) < n) n = int(avail);
  for (int i = 0; i < n; i++) out[i] = buf_[(r + i) & (N - 1)];
  r_.store(r + n);
  return n;
}

int RadioPlayer::count() {
  std::lock_guard<std::mutex> g(mu_);
  return n_;
}

bool RadioPlayer::source(int i, RadioSource& out) {
  std::lock_guard<std::mutex> g(mu_);
  if (i < 0 || i >= n_) return false;
  out = src_[i];
  return true;
}

int RadioPlayer::current() {
  std::lock_guard<std::mutex> g(mu_);
  for (int i = 0; i < n_; i++) if (!std::strcmp(src_[i].id, cur_)) return i;
  return -1;
}

void RadioPlayer::queue(const char* fmt, const char* arg) {
  // unter mu_ aufrufen; ist die Liste voll, fällt der älteste Befehl weg
  if (nAct_ == 4) { for (int i = 1; i < 4; i++) std::memcpy(actions_[i - 1], actions_[i], sizeof actions_[0]); nAct_ = 3; }
  snprintf(actions_[nAct_++], sizeof actions_[0], fmt, arg);
}

void RadioPlayer::select(int i) {
  std::lock_guard<std::mutex> g(mu_);
  if (i < 0 || i >= n_ || !std::strcmp(src_[i].id, cur_)) return;
  snprintf(cur_, sizeof cur_, "%s", src_[i].id);
  if (playing_) {
    gen_++;
    ring.clear();
    if (src_[i].spotify) queue("/api/radio?aktion=play&quelle=%s", cur_);
    else queue("/api/radio?aktion=pause");   // Spotify anhalten, falls es lief
  }
}

void RadioPlayer::play() {
  std::lock_guard<std::mutex> g(mu_);
  int i = -1;
  for (int k = 0; k < n_; k++) if (!std::strcmp(src_[k].id, cur_)) i = k;
  if (i < 0) { if (!n_) return; i = 0; snprintf(cur_, sizeof cur_, "%s", src_[0].id); }
  playing_ = true;
  fade_ = 1;
  gen_++;
  ring.clear();
  sleepLeft_ = sleepMin > 0 ? sleepMin * 60.f : 0;
  queue("/api/radio?aktion=play&quelle=%s", cur_);
}

void RadioPlayer::stop() {
  std::lock_guard<std::mutex> g(mu_);
  if (!playing_) return;
  playing_ = false;
  sleepLeft_ = 0;
  gen_++;
  ring.clear();
  queue("/api/radio?aktion=pause");
}

void RadioPlayer::skip() {
  std::lock_guard<std::mutex> g(mu_);
  queue("/api/radio?aktion=weiter");
}

void RadioPlayer::setVolume(int v) {
  vol_ = v < 0 ? 0 : v > maxVol_ ? maxVol_ : v;
}

void RadioPlayer::hint(char* out, int n) {
  std::lock_guard<std::mutex> g(mu_);
  snprintf(out, n, "%s", hint_);
}

void RadioPlayer::tick(float dt) {
  if (playing_ && sleepLeft_ > 0) {
    sleepLeft_ -= dt;
    fade_ = sleepLeft_ < FADE_SECS ? (sleepLeft_ > 0 ? sleepLeft_ / FADE_SECS : 0) : 1;
    if (sleepLeft_ <= 0) { stop(); fade_ = 1; }
  }
  if (vol_ > maxVol_) vol_ = maxVol_;
  const float v = vol_ / 100.f;
  gain_.store(playing_ ? v * v * fade_ : 0);
}

void RadioPlayer::mix(int16_t* mono, int n) {
  const float g = gain_.load();
  if (g <= 0) { ring.clear(); level_ = 0; return; }
  int16_t tmp[256];
  float peak = 0;
  for (int done = 0; done < n;) {
    const int want = n - done < 256 ? n - done : 256;
    const int got = ring.read(tmp, want);
    if (!got) break;   // Puffer leer: Stille
    for (int i = 0; i < got; i++) {
      const float s = tmp[i] * g;
      const float a = s < 0 ? -s : s;
      if (a > peak) peak = a;
      const int v = mono[done + i] + int(s);
      mono[done + i] = int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    done += got;
  }
  const float l = peak / 12000.f;
  level_ = level_ * 0.8f + (l > 1 ? 1 : l) * 0.2f;
}

// ---------------- Netz-Thread ----------------

void RadioPlayer::parse(const char* text) {
  std::lock_guard<std::mutex> g(mu_);
  RadioSource fresh[MAX_SOURCES];
  int n = 0;
  hint_[0] = 0;
  for (const char* p = text; *p;) {
    const char* e = std::strchr(p, '\n');
    const int len = e ? int(e - p) : int(std::strlen(p));
    char line[128];
    snprintf(line, sizeof line, "%.*s", len < 127 ? len : 127, p);
    p += len + (e ? 1 : 0);
    char* rest = std::strchr(line, ' ');
    if (!rest) continue;
    *rest++ = 0;
    if (!std::strcmp(line, "zeit")) epoch = std::atoll(rest);
    else if (!std::strcmp(line, "sperre")) std::sscanf(rest, "%d %d %d", &lockMode, &lockFrom, &lockTo);
    else if (!std::strcmp(line, "laut")) maxVol_ = std::atoi(rest);
    else if (!std::strcmp(line, "schlaf")) sleepMin = std::atoi(rest);
    else if (!std::strcmp(line, "hinweis")) snprintf(hint_, sizeof hint_, "%s", rest);
    else if (!std::strcmp(line, "apps")) appsOff = std::atol(rest);
    else if (!std::strcmp(line, "album")) {
      char t[24] = "";
      unsigned long v = 0;
      if (std::sscanf(rest, "%23s %lx", t, &v) >= 1) { snprintf(albumTheme, sizeof albumTheme, "%s", t); albumVersion = uint32_t(v); }
    }
    else if (!std::strcmp(line, "quelle") && n < MAX_SOURCES) {
      // quelle <id> <art> <name …>
      char* art = std::strchr(rest, ' ');
      if (!art) continue;
      *art++ = 0;
      char* name = std::strchr(art, ' ');
      if (!name) continue;
      *name++ = 0;
      RadioSource& s = fresh[n++];
      snprintf(s.id, sizeof s.id, "%s", rest);
      snprintf(s.name, sizeof s.name, "%s", name);
      s.spotify = !std::strcmp(art, "spotify");
    }
  }
  std::memcpy(src_, fresh, sizeof(RadioSource) * n);
  n_ = n;
  bool found = false;
  for (int i = 0; i < n_; i++) found |= !std::strcmp(src_[i].id, cur_);
  if (!found && cur_[0]) { cur_[0] = 0; if (playing_) { playing_ = false; gen_++; } }   // Sender wurde im Portal entfernt
}

void RadioPlayer::stateQuery(char* out, int n) {
  std::lock_guard<std::mutex> g(mu_);
  snprintf(out, n, "/api/geraet?radio=%s&spielt=%d&laut=%d&%s", cur_, playing_ ? 1 : 0, vol_, deviceState_);
}

void RadioPlayer::setDeviceState(const char* s) {
  std::lock_guard<std::mutex> g(mu_);
  snprintf(deviceState_, sizeof deviceState_, "%s", s);
}

void RadioPlayer::queueAction(const char* path) {
  std::lock_guard<std::mutex> g(mu_);
  queue("%s", path);
}

bool RadioPlayer::takeAction(char* out, int n) {
  std::lock_guard<std::mutex> g(mu_);
  if (!nAct_) return false;
  snprintf(out, n, "%s", actions_[0]);
  for (int i = 1; i < nAct_; i++) std::memcpy(actions_[i - 1], actions_[i], sizeof actions_[0]);
  nAct_--;
  return true;
}

uint32_t RadioPlayer::wantedStream(char* path, int n) {
  std::lock_guard<std::mutex> g(mu_);
  path[0] = 0;
  if (playing_ && cur_[0]) {
    bool sp = false;
    for (int i = 0; i < n_; i++) if (!std::strcmp(src_[i].id, cur_)) sp = src_[i].spotify;
    if (sp) snprintf(path, n, "/radio/spotify.pcm");
    else snprintf(path, n, "/radio/%s.pcm", cur_);
  }
  return gen_;
}

void RadioPlayer::streamEnded(uint32_t gen) {
  std::lock_guard<std::mutex> g(mu_);
  if (gen == gen_ && playing_) { playing_ = false; gen_++; }   // Aufnahme ist zu Ende
}

}  // namespace dk
