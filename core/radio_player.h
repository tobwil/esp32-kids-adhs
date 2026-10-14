// Elternradio: Sender (eigene Aufnahmen, Spotify-Playlists) kommen vom Eltern-Portal im Heimnetz (tools/portal).
// Plattformunabhängig: Die Kachel bedient RadioPlayer, ein Netz-Thread der Plattform (Firmware: WLAN, Simulator: Sockets)
// fragt das Portal ab, schickt Befehle und schreibt den Tonstrom in den Ringpuffer, der Audio-Thread mischt ihn dazu.
//
// Portal-Antwort auf GET /api/geraet (eine Angabe pro Zeile, Felder durch Leerzeichen getrennt, der Name zuletzt):
//   zeit <unix-sekunden, ortszeit>
//   sperre <0 aus|1 nur Radio|2 Zeitplan> <von min> <bis min>
//   laut <max 0..100>
//   schlaf <minuten, 0 = aus>
//   quelle <id> <aufnahme|spotify> <name …>
//   teile <id> <n>              mehrteilige Aufnahme (Hörspiel-CD): Teile /radio/<id>_1.pcm … _n.pcm
//   hinweis <text …>            (z. B. „Spotify ist nicht verbunden“)
//   apps <maske>                abgeschaltete Kacheln (Bit je Index)
//   album <thema> <version>     aktives Kartenthema (eingebaut: „pferde 0“), Version ändert sich bei jeder Bearbeitung
//   wecker <an> <min> <tage> <ton>  Wecker: Minuten seit Mitternacht, Tage als Bitmaske (Bit 0 = Montag), Ton „melodie“ oder Sender-ID
//   nacht <0|1>                 Nachtlicht mit Uhr in der Schlafenszeit
//   fragen <version>            Rätsel-Fragen aus dem Portal (GET /fragen.txt), Version ändert sich bei jeder Bearbeitung
//   datei <id> <bytes>          Aufnahme zum Mitnehmen auf die SD-Karte (Offline-Bibliothek)
//   nachricht …, kontakt …      siehe messages.h
// Ton: GET /radio/<id>.pcm liefert 16 Bit mono mit Synth::RATE (Spotify: ein gemeinsamer Live-Strom /radio/spotify.pcm).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

#include "messages.h"

namespace dk {

// Ein Schreiber (Netz), ein Leser (Audio), ohne Sperre
class PcmRing {
 public:
  static constexpr int N = 1 << 15;   // 32768 Samples ≈ 2 s bei 16 kHz
  int space() const { return N - 1 - fill(); }
  int fill() const { return int((w_.load() - r_.load()) & (N - 1)); }
  void write(const int16_t* s, int n);
  int read(int16_t* out, int n);   // gibt gelesene Anzahl zurück
  void clear() { r_.store(w_.load()); }

 private:
  int16_t buf_[N];
  std::atomic<uint32_t> w_{0}, r_{0};
};

struct RadioSource {
  char id[32];
  char name[48];
  bool spotify;
  int tracks;   // mehrteilig (Hörspiel-CD, Album): Teile 1..tracks unter /radio/<id>_<k>.pcm; 0 = eine Datei /radio/<id>.pcm
};

class RadioPlayer {
 public:
  static constexpr int MAX_SOURCES = 12;

  // ---- Kachel (Kern 1) ----
  int count();
  bool source(int i, RadioSource& out);
  int current();                      // Index des gewählten Senders (-1 = keiner)
  void select(int i);                 // Sender wählen (spielt, falls gerade gespielt wird)
  void play();
  void stop();
  bool playing() const { return playing_; }
  void skip();                        // nächstes Lied (Spotify) bzw. nächster Teil
  int track() const { return track_; } // aktueller Teil (1-basiert) des gewählten Senders
  // Clip dazwischen (Sprachnachricht): das Radio pausiert und läuft danach weiter
  void playClip(const char* path);
  bool clipPlaying() const { return clip_; }
  void stopClip();
  void setVolume(int v);              // 0..100 (begrenzt durch die Eltern)
  int volume() const { return vol_; }
  int maxVolume() const { return maxVol_; }
  float sleepLeft() const { return sleepLeft_; }   // s bis zum Ausschalten (0 = kein Timer)
  bool online() const { return online_; }
  bool buffering() const { return playing_ && ring.fill() < 800; }
  void hint(char* out, int n);        // Hinweis vom Portal (leer = keiner)
  void tick(float dt);                // Schlaf-Timer, Ausblenden
  float level() const { return level_; }   // aktuelle Lautheit 0..1 (für die Anzeige)

  // ---- Netz-Thread ----
  // Antwort des Portals übernehmen
  void parse(const char* text);
  // Abfrage an das Portal mit dem Zustand des Geräts (für die Anzeige im Portal)
  void stateQuery(char* out, int n);
  // nächster Befehl ans Portal (z. B. „/api/radio?aktion=play&quelle=a3“), false = keiner
  bool takeAction(char* out, int n);
  // Welcher Strom gerade laufen soll: Pfad (leer = keiner) und Generation (ändert sich bei jedem Wechsel)
  uint32_t wantedStream(char* path, int n);
  void streamEnded(uint32_t gen);     // Aufnahme zu Ende
  void setOnline(bool on) { online_ = on; }
  PcmRing ring;

  // vom Portal (für Gerät und Sperre)
  int64_t epoch = 0;                  // 0 = unbekannt
  int lockMode = -1, lockFrom = 0, lockTo = 0;   // -1 = noch nichts vom Portal
  int sleepMin = 0;
  long appsOff = -1;                  // -1 = noch nichts vom Portal
  bool alarmOn = false, nightLight = false;
  int alarmMin = 7 * 60, alarmDays = 0x1F;
  char alarmTone[32] = "melodie";
  struct File { char id[32]; long bytes; };
  static constexpr int MAX_FILES = 12;
  File files[MAX_FILES];
  int nFiles = 0;
  void setMessages(Messages* m) { msgs_ = m; }
  char albumTheme[24] = "";
  uint32_t albumVersion = 0;
  uint32_t quizVersion = 0;           // 0 = Portal hat (noch) keinen Katalog gemeldet

  // ---- Audio-Thread ----
  void mix(int16_t* mono, int n);     // Radio dazumischen

  // vom Gerät gemeldeter Zustand fürs Portal, als Abfrageteil („kachel=…&sterne=…“)
  void setDeviceState(const char* s);
  // beliebiger Befehl ans Portal (z. B. „/api/sperre?modus=1“)
  void queueAction(const char* path);

 private:
  char deviceState_[96] = "";
  Messages* msgs_ = nullptr;
  bool clip_ = false, resume_ = false;
  char clipPath_[48] = "";
  void queue(const char* fmt, const char* arg = "");

  std::mutex mu_;
  RadioSource src_[MAX_SOURCES];
  int n_ = 0;
  char cur_[32] = "";
  int track_ = 1;
  bool playing_ = false;
  int vol_ = 40, maxVol_ = 80;
  float sleepLeft_ = 0, fade_ = 1;
  bool online_ = false;
  uint32_t gen_ = 1;
  char hint_[64] = "";
  char actions_[4][80] = {};
  int nAct_ = 0;
  float level_ = 0;
  std::atomic<float> gain_{0};
};

}  // namespace dk
