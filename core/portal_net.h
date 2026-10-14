// Verbindung zum Eltern-Portal, für Gerät und Simulator gemeinsam: HTTP/1.0 mit optionalem Schlüssel
// („Authorization: Bearer …“), Chunked-Antworten werden verstanden. Die Verbindung selbst (Socket, TLS) liefert die
// Plattform über netOpen/netWrite/netRead/netClose. Alle Funktionen blockieren und laufen in eigenen Threads/Tasks.
#pragma once
#include "radio_player.h"

namespace dk {

// Wohin: Mac-Portal im Heimnetz (http, Port 8770) oder Cloud-Portal (https, Port 443, mit Schlüssel)
struct Endpoint {
  char host[64] = "";
  int port = 8770;
  bool tls = false;
  char key[64] = "";
  bool valid() const { return host[0] != 0; }
};

// ---- von der Plattform (Gerät: firmware/net_client.cpp, Simulator: sim/net_posix.cpp) ----
struct NetConn;
NetConn* netOpen(const Endpoint& ep, int timeoutMs);
int netWrite(NetConn* c, const void* data, int n);          // geschriebene Bytes, -1 = Fehler
int netRead(NetConn* c, void* buf, int n, int timeoutMs);   // >0 Bytes, 0 = Ende, -1 = Fehler, -2 = Zeitlimit
void netClose(NetConn* c);

// Befehle ans Portal schicken (Radio, „gehört“, aufgenommene Nachricht), dann den Zustand abfragen und übernehmen.
// true = Portal erreichbar
bool portalPoll(RadioPlayer& p, Messages* m, const Endpoint& ep);

// Daten per POST schicken (z. B. eine Sprachnachricht). true = Status 200
bool portalPost(const Endpoint& ep, const char* path, const uint8_t* data, int len);

// Datei holen und stückweise an sink geben (sink gibt false zurück = abbrechen). true = vollständig (Status 200)
bool portalDownload(const Endpoint& ep, const char* path, bool (*sink)(void* ctx, const uint8_t* data, int n), void* ctx);

// Dateien auf dem Gerät (SD-Karte): Strompfade wie „/radio/a1.pcm“ werden zuerst dort gesucht
struct LocalFiles {
  virtual ~LocalFiles() = default;
  virtual bool open(const char* path) = 0;
  virtual int read(uint8_t* buf, int n) = 0;   // 0 = Ende
  virtual void close() = 0;
};

// Strom-Schleife (kehrt nie zurück): öffnet den Strom, den der Player gerade will, und füllt den Ringpuffer.
// endpoint() liefert die aktuelle Adresse (ungültig = noch kein Netz).
void portalStreamLoop(RadioPlayer& p, Endpoint (*endpoint)(), LocalFiles* local = nullptr);

}  // namespace dk
