#include "messages.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dk {

void Messages::beginParse() {
  std::lock_guard<std::mutex> g(mu_);
  nFresh_ = nFreshCon_ = 0;
}

bool Messages::parseLine(const char* key, char* rest) {
  std::lock_guard<std::mutex> g(mu_);
  if (!std::strcmp(key, "nachricht")) {   // nachricht <id> <gehört> <von …>
    char* h = std::strchr(rest, ' ');
    if (!h || nFresh_ >= MAX_IN) return true;
    *h++ = 0;
    char* from = std::strchr(h, ' ');
    Message& m = fresh_[nFresh_++];
    snprintf(m.id, sizeof m.id, "%s", rest);
    m.heard = *h == '1';
    snprintf(m.from, sizeof m.from, "%s", from ? from + 1 : "");
    for (int i = 0; i < nHeard_; i++) if (!std::strcmp(heardQueue_[i], m.id)) m.heard = true;   // Meldung noch unterwegs
    return true;
  }
  if (!std::strcmp(key, "kontakt")) {   // kontakt <id> <name …>
    char* name = std::strchr(rest, ' ');
    if (!name || nFreshCon_ >= MAX_CONTACTS) return true;
    *name++ = 0;
    Contact& c = freshCon_[nFreshCon_++];
    snprintf(c.id, sizeof c.id, "%s", rest);
    snprintf(c.name, sizeof c.name, "%s", name);
    return true;
  }
  return false;
}

void Messages::endParse() {
  std::lock_guard<std::mutex> g(mu_);
  for (int i = 0; i < nFresh_; i++) {   // lokal schon als gehört markiert: so lassen
    for (int k = 0; k < nIn_; k++)
      if (!std::strcmp(in_[k].id, fresh_[i].id) && in_[k].heard) fresh_[i].heard = true;
  }
  std::memcpy(in_, fresh_, sizeof(Message) * nFresh_);
  nIn_ = nFresh_;
  std::memcpy(con_, freshCon_, sizeof(Contact) * nFreshCon_);
  nCon_ = nFreshCon_;
  // neue ungehörte Nachricht? (die Liste ist neueste zuerst)
  for (int i = 0; i < nIn_; i++)
    if (!in_[i].heard) {
      if (std::strcmp(lastNewest_, in_[i].id)) { arrived_ = true; snprintf(lastNewest_, sizeof lastNewest_, "%s", in_[i].id); }
      break;
    }
}

int Messages::count() {
  std::lock_guard<std::mutex> g(mu_);
  return nIn_;
}

bool Messages::get(int i, Message& out) {
  std::lock_guard<std::mutex> g(mu_);
  if (i < 0 || i >= nIn_) return false;
  out = in_[i];
  return true;
}

int Messages::unheard() {
  std::lock_guard<std::mutex> g(mu_);
  int n = 0;
  for (int i = 0; i < nIn_; i++) n += !in_[i].heard;
  return n;
}

bool Messages::newest(Message& out) {
  std::lock_guard<std::mutex> g(mu_);
  for (int i = 0; i < nIn_; i++)
    if (!in_[i].heard) { out = in_[i]; return true; }
  return false;
}

void Messages::markHeard(const char* id) {
  std::lock_guard<std::mutex> g(mu_);
  for (int i = 0; i < nIn_; i++) {
    if (std::strcmp(in_[i].id, id) || in_[i].heard) continue;
    in_[i].heard = true;
    if (nHeard_ < 4) snprintf(heardQueue_[nHeard_++], sizeof heardQueue_[0], "%s", id);
  }
}

int Messages::contacts() {
  std::lock_guard<std::mutex> g(mu_);
  return nCon_;
}

bool Messages::contact(int i, Contact& out) {
  std::lock_guard<std::mutex> g(mu_);
  if (i < 0 || i >= nCon_) return false;
  out = con_[i];
  return true;
}

bool Messages::startRecording() {
  if (!rec_ || pendingSend_) return false;
  len_ = 0;
  recording_ = true;
  return true;
}

void Messages::stopRecording() { recording_ = false; }

void Messages::feed(const int16_t* mono, int n) {
  if (!recording_) return;
  int len = len_.load();
  const int lim = cap_ < RATE * MAX_SECS ? cap_ : RATE * MAX_SECS;
  if (len + n > lim) { n = lim - len; recording_ = false; }   // höchstens 30 s
  if (n > 0) { std::memcpy(rec_ + len, mono, size_t(n) * 2); len_ = len + n; }
}

bool Messages::send(const char* contactId) {
  std::lock_guard<std::mutex> g(mu_);
  if (recording_ || len_ < RATE / 2) return false;   // kürzer als eine halbe Sekunde: nichts senden
  snprintf(to_, sizeof to_, "%s", contactId);
  pendingSend_ = true;
  return true;
}

bool Messages::takeUpload(char* path, int n, const int16_t** data, int* samples) {
  std::lock_guard<std::mutex> g(mu_);
  if (!pendingSend_ || uploading_) return false;
  uploading_ = true;
  snprintf(path, n, "/api/nachricht?an=%s", to_);
  *data = rec_;
  *samples = len_;
  return true;
}

void Messages::uploadDone(bool ok) {
  std::lock_guard<std::mutex> g(mu_);
  uploading_ = false;
  if (ok) { pendingSend_ = false; len_ = 0; sent_++; }   // sonst beim nächsten Mal noch einmal
}

bool Messages::takeAction(char* out, int n) {
  std::lock_guard<std::mutex> g(mu_);
  if (!nHeard_) return false;
  snprintf(out, n, "/api/nachricht/gehoert?id=%s", heardQueue_[0]);
  for (int i = 1; i < nHeard_; i++) std::memcpy(heardQueue_[i - 1], heardQueue_[i], sizeof heardQueue_[0]);
  nHeard_--;
  return true;
}

}  // namespace dk
