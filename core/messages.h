// Sprachnachrichten zwischen Kind und Eltern, nur mit Kontakten, die die Eltern im Portal freigegeben haben.
// Eingang: das Portal meldet Nachrichten („nachricht <id> <gehört 0|1> <von …>“) und Kontakte („kontakt <id> <name …>“),
// abgespielt wird über den RadioPlayer (Clip /nachricht/<id>.pcm, das Radio pausiert so lange).
// Ausgang: Aufnahme vom Mikrofon in einen Puffer (höchstens 30 s, 16 kHz mono), der Netz-Thread schickt sie
// per POST /api/nachricht?an=<kontakt> ans Portal. Plattformunabhängig, Puffer vom Aufrufer (Gerät: PSRAM).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

namespace dk {

struct Message {
  char id[16];
  char from[24];
  bool heard;
};

struct Contact {
  char id[16];
  char name[24];
};

class Messages {
 public:
  static constexpr int MAX_IN = 6, MAX_CONTACTS = 4;
  static constexpr int RATE = 16000, MAX_SECS = 30;

  void setBuffer(int16_t* buf, int samples) { rec_ = buf; cap_ = samples; }

  // ---- Portal → Gerät (Netz-Thread) ----
  void beginParse();
  bool parseLine(const char* key, char* rest);   // true = Zeile gehörte hierher
  void endParse();

  // ---- Kachel ----
  int count();
  bool get(int i, Message& out);
  int unheard();
  bool newest(Message& out);       // neueste ungehörte Nachricht
  void markHeard(const char* id);  // lokal sofort, das Portal erfährt es über takeAction
  int contacts();
  bool contact(int i, Contact& out);

  // Aufnahme
  bool startRecording();
  void stopRecording();
  bool recording() const { return recording_; }
  float recordedSecs() const { return float(len_) / RATE; }
  void discard() { len_ = 0; }
  bool send(const char* contactId);   // Aufnahme zum Senden vormerken
  bool sending() const { return pendingSend_; }
  int sentCount() const { return sent_; }   // steigt nach jeder erfolgreich gesendeten Nachricht

  // ---- Mikrofon (Audio-Thread) ----
  void feed(const int16_t* mono, int n);

  // ---- Netz-Thread ----
  bool takeUpload(char* path, int n, const int16_t** data, int* samples);
  void uploadDone(bool ok);
  bool takeAction(char* out, int n);   // „gehört“-Meldungen
  bool takeArrived() { const bool a = arrived_; arrived_ = false; return a; }   // neue Nachricht seit dem letzten Mal

 private:
  std::mutex mu_;
  Message in_[MAX_IN], fresh_[MAX_IN];
  Contact con_[MAX_CONTACTS], freshCon_[MAX_CONTACTS];
  int nIn_ = 0, nCon_ = 0, nFresh_ = 0, nFreshCon_ = 0;
  char heardQueue_[4][16] = {};
  int nHeard_ = 0;
  int16_t* rec_ = nullptr;
  int cap_ = 0;
  std::atomic<int> len_{0};
  std::atomic<bool> recording_{false};
  bool pendingSend_ = false, uploading_ = false;
  char to_[16] = "";
  int sent_ = 0;
  bool arrived_ = false;
  char lastNewest_[16] = "";
};

}  // namespace dk
