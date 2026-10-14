#include "net.h"

#include <DNSServer.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "portal_net.h"

using namespace dk;

static constexpr const char* SETUP_SSID = "Grummel-Setup";
static constexpr int DEFAULT_PORT = 8770;

static RadioPlayer* radio = nullptr;
static Messages* msgs = nullptr;
static bool sdOk = false;
static Preferences prefs;
static volatile bool wifiOn = true, setupOn = false, setupStop = false, setupStart = false;
static volatile bool reconnect = false;
// portalHost: eingestellte Adresse. Leer = Mac-Portal im Heimnetz über mDNS suchen, „name.local[:port]“ oder IP = Mac-Portal,
// „https://…“ = Cloud-Portal (mit Gerät-Schlüssel aus dem NVS)
static char ssid[33] = "", portalHost[96] = "";
static Endpoint ep;   // gefundenes Portal (ungültig = keins)
static portMUX_TYPE epMux = portMUX_INITIALIZER_UNLOCKED;
static bool cloud() { return !strncmp(portalHost, "https://", 8); }
static uint32_t lastFind = 0;

static DNSServer* dns = nullptr;
static WebServer* web = nullptr;
static String scanHtml;

// ---------------- Einrichtungsseite ----------------

static String esc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static void scan() {
  const int n = WiFi.scanNetworks();
  scanHtml = "";
  for (int i = 0; i < n && i < 20; i++) {
    const String s = WiFi.SSID(i);
    if (!s.length() || scanHtml.indexOf("value=\"" + esc(s) + "\"") >= 0) continue;
    scanHtml += "<option value=\"" + esc(s) + "\">" + esc(s) + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " (offen)" : "") + "</option>";
  }
  WiFi.scanDelete();
}

static const char PAGE_HEAD[] PROGMEM =
    "<!doctype html><html lang=de><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Grummel einrichten</title><style>body{font:17px -apple-system,sans-serif;margin:0;background:#f6f4fb;color:#221d33}"
    "main{max-width:420px;margin:0 auto;padding:22px 16px}h1{font-size:22px}label{display:block;margin:14px 0 4px;font-weight:600}"
    "input,select{width:100%;font:inherit;padding:10px;border:1px solid #d8d2ea;border-radius:10px;box-sizing:border-box;background:#fff}"
    "button{margin-top:20px;width:100%;font:inherit;padding:12px;border:0;border-radius:12px;background:#6a52d9;color:#fff}"
    "p{color:#6d6880}</style></head><body><main>";

static void handleRoot() {
  String h = FPSTR(PAGE_HEAD);
  h += "<h1>Grummel ins WLAN bringen</h1><p>Heimnetz auswählen und das WLAN-Passwort eingeben. Es bleibt nur auf dem Gerät.</p>"
       "<form method=post action=/speichern><label>Netz</label><select name=n><option value=''>– bitte wählen –</option>";
  h += scanHtml;
  h += "</select><label>…oder Name von Hand</label><input name=m autocomplete=off autocapitalize=off>"
       "<label>Passwort</label><input name=p type=password autocomplete=off>"
       "<label>Eltern-Portal</label><input name=a placeholder='leer = Mac im Heimnetz, sonst https://…' autocapitalize=off value=\"";
  h += esc(String(portalHost));
  h += "\"><label>Gerät-Schlüssel (nur für das Cloud-Portal, leer = bisherigen behalten)</label><input name=k type=password autocomplete=off autocapitalize=off value=\"";
  h += "\"><button>Speichern und verbinden</button></form><p><a href=/neu>Netze neu suchen</a></p></main></body></html>";
  web->send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
  String s = web->arg("m").length() ? web->arg("m") : web->arg("n");
  s.trim();
  if (!s.length()) { web->send(400, "text/plain; charset=utf-8", "Bitte ein Netz wählen."); return; }
  prefs.putString("ssid", s);
  prefs.putString("pw", web->arg("p"));
  String a = web->arg("a");
  a.trim();
  prefs.putString("portal", a);
  String k = web->arg("k");
  k.trim();
  if (k.length()) prefs.putString("schluessel", k);
  snprintf(ssid, sizeof ssid, "%s", s.c_str());
  snprintf(portalHost, sizeof portalHost, "%s", a.c_str());
  String h = FPSTR(PAGE_HEAD);
  h += "<h1>Gespeichert</h1><p>Grummel verbindet sich jetzt mit „" + esc(s) + "“. Dieses Setup-WLAN schließt sich gleich, "
       "du kannst dein Handy wieder ins Heimnetz wechseln.</p></main></body></html>";
  web->send(200, "text/html; charset=utf-8", h);
  setupStop = true;
}

static void startSetup() {
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_SSID);
  scan();
  dns = new DNSServer();
  dns->start(53, "*", WiFi.softAPIP());
  web = new WebServer(80);
  web->on("/", handleRoot);
  web->on("/speichern", HTTP_POST, handleSave);
  web->on("/neu", [] { scan(); web->sendHeader("Location", "/"); web->send(302, "text/plain", ""); });
  // Handys prüfen bekannte Adressen: alles auf die Einrichtungsseite umleiten
  web->onNotFound([] { web->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/"); web->send(302, "text/plain", ""); });
  web->begin();
  setupOn = true;
}

static void endSetup() {
  if (web) { web->stop(); delete web; web = nullptr; }
  if (dns) { dns->stop(); delete dns; dns = nullptr; }
  WiFi.softAPdisconnect(true);
  setupOn = false;
  reconnect = true;
}

// ---------------- Verbindung ----------------

static void connect() {
  portENTER_CRITICAL(&epMux); ep = Endpoint(); portEXIT_CRITICAL(&epMux);
  if (!wifiOn || !ssid[0]) { WiFi.mode(WIFI_OFF); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("grummel");
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, prefs.getString("pw", "").c_str());
}

static void setEndpoint(const Endpoint& e) { portENTER_CRITICAL(&epMux); ep = e; portEXIT_CRITICAL(&epMux); }
static Endpoint endpoint() { portENTER_CRITICAL(&epMux); const Endpoint e = ep; portEXIT_CRITICAL(&epMux); return e; }

static void findPortal() {
  lastFind = millis();
  Endpoint e;
  if (cloud()) {   // https://host[/…]: der Name wird beim Verbinden aufgelöst
    snprintf(e.host, sizeof e.host, "%s", portalHost + 8);
    if (char* sl = strchr(e.host, '/')) *sl = 0;
    e.port = 443;
    e.tls = true;
    snprintf(e.key, sizeof e.key, "%s", prefs.getString("schluessel", "").c_str());
    setEndpoint(e);
    return;
  }
  static bool mdnsUp = false;
  if (!mdnsUp) mdnsUp = MDNS.begin("grummel");
  e.port = DEFAULT_PORT;
  if (portalHost[0]) {
    // feste Adresse: „name.local[:port]“ über mDNS, sonst Name oder IP direkt
    char h[96];
    snprintf(h, sizeof h, "%s", strncmp(portalHost, "http://", 7) ? portalHost : portalHost + 7);
    if (char* c = strchr(h, ':')) { e.port = atoi(c + 1); *c = 0; }
    const size_t n = strlen(h);
    if (n > 6 && !strcasecmp(h + n - 6, ".local")) {
      h[n - 6] = 0;
      const IPAddress ip = MDNS.queryHost(h, 2000);
      if (ip != IPAddress(0, 0, 0, 0)) snprintf(e.host, sizeof e.host, "%s", ip.toString().c_str());
    } else {
      snprintf(e.host, sizeof e.host, "%s", h);
    }
  } else if (MDNS.queryService("grummel", "tcp") > 0) {
    snprintf(e.host, sizeof e.host, "%s", MDNS.address(0).toString().c_str());
    e.port = MDNS.port(0);
  }
  setEndpoint(e);
}



// ---------------- Sammelalbum vom Portal ----------------
// /stand.txt: „<thema> <version>“ des geladenen Themas. Neues Thema: erst nach /aneu laden, dann gegen /album tauschen.
static volatile bool albumReload = false;
static int albumOwn = -1;   // -1: noch nicht nachgesehen

static String readStand() {
  File f = LittleFS.open("/stand.txt", "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  s.trim();
  return s;
}

static void writeStand(const char* s) {
  File f = LittleFS.open("/stand.txt", "w");
  if (f) { f.print(s); f.close(); }
}

static void clearDir(const char* dir) {
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) return;
  String names[64];
  int n = 0;
  for (File f = d.openNextFile(); f && n < 64; f = d.openNextFile()) { names[n++] = String(dir) + "/" + f.name(); f.close(); }
  d.close();
  for (int i = 0; i < n; i++) LittleFS.remove(names[i]);
  LittleFS.rmdir(dir);
}

static bool toFile(void* ctx, const uint8_t* data, int n) { return static_cast<File*>(ctx)->write(data, n) == size_t(n); }

static bool downloadTo(fs::FS& fs, const char* path, const char* file) {
  File f = fs.open(file, "w");
  if (!f) return false;
  const bool ok = portalDownload(endpoint(), path, toFile, &f);
  f.close();
  if (!ok) fs.remove(file);
  return ok;
}

static bool download(const char* path, const char* file) {
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < 80 * 1024) return false;   // Platz für Datei und Dateisystem lassen
  return downloadTo(LittleFS, path, file);
}

// ---------------- Offline-Bibliothek auf der SD-Karte ----------------
// /radio/<id>.pcm: Aufnahmen aus dem Portal, /radio/quellen.txt: Senderliste (ohne Spotify) für den Start ohne Netz,
// /nachricht/<id>.pcm: Sprachnachrichten. Der Tonstrom liest zuerst von hier (SdFiles), sonst vom Portal.

struct SdFiles : LocalFiles {
  File f;
  bool open(const char* path) override {
    if (!sdOk || !SD_MMC.exists(path)) return false;
    f = SD_MMC.open(path, "r");
    return bool(f);
  }
  int read(uint8_t* buf, int n) override { return f ? int(f.read(buf, n)) : 0; }
  void close() override { if (f) f.close(); }
};
static SdFiles sdFiles;

static void cleanDir(const char* dir, bool (*keep)(const char* name)) {
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return;
  String del[16];
  int n = 0;
  for (File f = d.openNextFile(); f && n < 16; f = d.openNextFile()) {
    if (!keep(f.name())) del[n++] = String(dir) + "/" + f.name();
    f.close();
  }
  d.close();
  for (int i = 0; i < n; i++) SD_MMC.remove(del[i]);
}

static bool keepRadio(const char* name) {
  if (!strcmp(name, "quellen.txt")) return true;
  for (int i = 0; i < radio->nFiles; i++) {
    char f[48];
    snprintf(f, sizeof f, "%s.pcm", radio->files[i].id);
    if (!strcmp(name, f)) return true;
  }
  return false;
}

static bool keepMsg(const char* name) {
  for (int i = 0; i < msgs->count(); i++) {
    Message m;
    char f[32];
    if (msgs->get(i, m) && (snprintf(f, sizeof f, "%s.pcm", m.id), !strcmp(name, f))) return true;
  }
  return false;
}

static void syncOffline() {
  if (!sdOk) return;
  SD_MMC.mkdir("/radio");
  SD_MMC.mkdir("/nachricht");
  // Senderliste für den Start ohne Netz (Spotify geht offline nicht)
  String list;
  for (int i = 0; i < radio->count(); i++) {
    RadioSource s;
    if (radio->source(i, s) && !s.spotify) list += String("quelle ") + s.id + " aufnahme " + s.name + "\n";
  }
  File old = SD_MMC.open("/radio/quellen.txt", "r");
  const String was = old ? old.readString() : String();
  if (old) old.close();
  if (was != list) { File f = SD_MMC.open("/radio/quellen.txt", "w"); if (f) { f.print(list); f.close(); } }
  // je Abfrage höchstens eine Datei holen, damit Abfragen und Ton nicht lange warten
  for (int i = 0; i < radio->nFiles; i++) {
    char path[48], tmp[48];
    snprintf(path, sizeof path, "/radio/%s.pcm", radio->files[i].id);
    File f = SD_MMC.open(path, "r");
    const long have = f ? long(f.size()) : -1;
    if (f) f.close();
    if (have == radio->files[i].bytes) continue;
    snprintf(tmp, sizeof tmp, "/radio/%s.tmp", radio->files[i].id);
    if (downloadTo(SD_MMC, path, tmp)) { SD_MMC.remove(path); SD_MMC.rename(tmp, path); }
    return;
  }
  for (int i = 0; i < msgs->count(); i++) {
    Message m;
    char path[40];
    if (!msgs->get(i, m)) continue;
    snprintf(path, sizeof path, "/nachricht/%s.pcm", m.id);
    if (SD_MMC.exists(path)) continue;
    char tmp[40];
    snprintf(tmp, sizeof tmp, "/nachricht/%s.tmp", m.id);
    if (downloadTo(SD_MMC, path, tmp)) SD_MMC.rename(tmp, path);
    return;
  }
  cleanDir("/radio", keepRadio);
  cleanDir("/nachricht", keepMsg);
}

// beim Start ohne Netz: Sender von der SD-Karte
static void loadOfflineSources() {
  if (!sdOk) return;
  File f = SD_MMC.open("/radio/quellen.txt", "r");
  if (!f) return;
  const String s = f.readString();
  f.close();
  if (s.length()) radio->parse(s.c_str());
}

static void syncAlbum() {
  if (!radio->albumTheme[0] || !LittleFS.totalBytes()) return;
  char want[48];
  snprintf(want, sizeof want, "%s %08lx", radio->albumTheme, (unsigned long)radio->albumVersion);
  if (readStand() == want) return;
  if (!strcmp(radio->albumTheme, "pferde")) {   // zurück zu den eingebauten Pferden
    writeStand(want);
    albumOwn = false;
    albumReload = true;
    return;
  }
  clearDir("/aneu");
  LittleFS.mkdir("/aneu");
  if (!download("/album/karten.txt", "/aneu/karten.txt")) { clearDir("/aneu"); return; }
  // Bilder laut Kopfzeile „#bilder 1,2,5“
  File t = LittleFS.open("/aneu/karten.txt", "r");
  String ids;
  while (t.available()) {
    const String line = t.readStringUntil('\n');
    if (line.startsWith("#bilder ")) { ids = line.substring(8); break; }
  }
  t.close();
  int start = 0;
  while (start < int(ids.length())) {
    int end = ids.indexOf(',', start);
    if (end < 0) end = ids.length();
    const int id = ids.substring(start, end).toInt();
    start = end + 1;
    if (id <= 0) continue;
    char path[40], file[24];
    snprintf(path, sizeof path, "/album/bild/%d.jpg", id);
    snprintf(file, sizeof file, "/aneu/%d.jpg", id);
    if (!download(path, file)) { clearDir("/aneu"); return; }   // nächster Versuch bei der nächsten Abfrage
  }
  clearDir("/album");
  LittleFS.rename("/aneu", "/album");
  writeStand(want);
  albumOwn = true;
  albumReload = true;
  Serial.printf("album: Thema %s geladen\n", radio->albumTheme);
}

// ---------------- Rätsel-Fragen vom Portal ----------------
// /quiz.txt wie beim Hochladen per USB (tools/quiz.py), /quiz.ver merkt sich die Version des Portals

static volatile bool quizReload = false;

static void syncQuiz() {
  if (!radio->quizVersion || !LittleFS.totalBytes()) return;
  char want[12];
  snprintf(want, sizeof want, "%08lx", (unsigned long)radio->quizVersion);
  File v = LittleFS.open("/quiz.ver", "r");
  const String have = v ? v.readString() : String();
  if (v) v.close();
  if (have == want) return;
  if (!download("/fragen.txt", "/quiz.tmp")) return;
  LittleFS.remove("/quiz.txt");
  LittleFS.rename("/quiz.tmp", "/quiz.txt");
  File w = LittleFS.open("/quiz.ver", "w");
  if (w) { w.print(want); w.close(); }
  quizReload = true;
  Serial.println("fragen: neuer Katalog vom Portal");
}

bool netTakeQuizReload() { const bool r = quizReload; quizReload = false; return r; }

bool netAlbumTheme() {
  if (albumOwn < 0) {   // geladenes Thema vom letzten Mal (vor netBegin abfragbar)
    const String st = readStand();
    albumOwn = st.length() && !st.startsWith("pferde ") && LittleFS.exists("/album/karten.txt");
  }
  return albumOwn > 0;
}
bool netTakeAlbumReload() { const bool r = albumReload; albumReload = false; return r; }

static void netTask(void*) {
  connect();
  for (;;) {
    if (setupStart) { setupStart = false; startSetup(); }
    if (setupOn) {
      dns->processNextRequest();
      web->handleClient();
      if (setupStop) { setupStop = false; vTaskDelay(pdMS_TO_TICKS(1500)); endSetup(); }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (reconnect) { reconnect = false; WiFi.disconnect(true); vTaskDelay(pdMS_TO_TICKS(200)); connect(); }
    if (!wifiOn || !ssid[0] || WiFi.status() != WL_CONNECTED) {
      radio->setOnline(false);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!endpoint().valid() || (!radio->online() && millis() - lastFind > 30000)) findPortal();
    if (!endpoint().valid()) { radio->setOnline(false); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
    if (portalPoll(*radio, msgs, endpoint())) { syncAlbum(); syncQuiz(); syncOffline(); }
    // Cloud: seltener fragen (kostenloser Rahmen von R2), beim Abspielen öfter (Befehle kommen schneller an)
    vTaskDelay(pdMS_TO_TICKS(radio->playing() ? (cloud() ? 2000 : 1000) : (cloud() ? 5000 : 2000)));
  }
}

static void streamTask(void*) { portalStreamLoop(*radio, endpoint, &sdFiles); }

void netBegin(RadioPlayer* r, Messages* m, bool sd) {
  radio = r;
  msgs = m;
  sdOk = sd;
  loadOfflineSources();
  prefs.begin("wlan", false);
  wifiOn = prefs.getBool("an", true);
  snprintf(ssid, sizeof ssid, "%s", prefs.getString("ssid", "").c_str());
  snprintf(portalHost, sizeof portalHost, "%s", prefs.getString("portal", "").c_str());
  // TLS braucht Stapel: Abfrage 16 KB, Tonstrom 12 KB
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(streamTask, "radio", 12288, nullptr, 2, nullptr, 0);
}

void netSetPortal(const char* addr, const char* key) {
  snprintf(portalHost, sizeof portalHost, "%s", addr);
  prefs.putString("portal", portalHost);
  if (key && *key) prefs.putString("schluessel", key);
  lastFind = 0;
  setEndpoint(Endpoint());   // beim nächsten Durchlauf neu suchen
}

void netStatus(char* out, int n) {
  snprintf(out, n, "wlan %s, netz „%s“, ip %s, portal %s:%d %s", !wifiOn ? "aus" : setupOn ? "einrichten" : WiFi.status() == WL_CONNECTED ? "verbunden" : "getrennt",
           ssid, WiFi.localIP().toString().c_str(), endpoint().valid() ? endpoint().host : "-", endpoint().port, radio && radio->online() ? "erreichbar" : "nicht erreichbar");
}

// ---------------- Gerätemenü in den Einstellungen ----------------

struct NetMenu : SystemMenu {
  // Einrichten: Erklärung + Abbrechen; sonst: Zustand, Einrichten, an/aus, Portal, Adresse
  int count() override { return setupOn || setupStart ? 6 : 5; }
  bool info(int i) override { return setupOn || setupStart ? i < 5 : (i == 0 || i >= 3); }
  void text(int i, char* b, int n) override {
    if (setupOn || setupStart) {
      static const char* const L[5] = {"Am Handy mit dem WLAN", "„Grummel-Setup“ verbinden.", "Die Seite öffnet sich von selbst",
                                       "(sonst 192.168.4.1 aufrufen).", "Dort Heimnetz und Passwort wählen."};
      if (i < 5) snprintf(b, n, "%s", L[i]);
      else snprintf(b, n, "Einrichten abbrechen");
      return;
    }
    switch (i) {
      case 0:
        if (!wifiOn) snprintf(b, n, "WLAN ist aus");
        else if (!ssid[0]) snprintf(b, n, "WLAN ist noch nicht eingerichtet");
        else {   // langer Netzname: gekürzt, damit die Zeile auf den Bildschirm passt
          char sh[24];
          snprintf(sh, sizeof sh, strlen(ssid) > 16 ? "%.15s…" : "%s", ssid);
          snprintf(b, n, WiFi.status() == WL_CONNECTED ? "Verbunden mit „%s“" : "Verbinde mit „%s“ …", sh);
        }
        break;
      case 1: snprintf(b, n, ssid[0] ? "WLAN neu einrichten" : "WLAN einrichten"); break;
      case 2: snprintf(b, n, "WLAN: %s", wifiOn ? "an" : "aus"); break;
      case 3: snprintf(b, n, "Eltern-Portal: %s%s", radio && radio->online() ? "verbunden" : "nicht gefunden", cloud() ? " (Cloud)" : ""); break;
      default:
        if (WiFi.status() == WL_CONNECTED) snprintf(b, n, "Adresse: %s", WiFi.localIP().toString().c_str());
        else snprintf(b, n, " ");
        break;
    }
  }
  void tap(int i) override {
    if (setupOn || setupStart) { if (i == 5) setupStop = true; return; }
    if (i == 1) { wifiOn = true; prefs.putBool("an", true); setupStart = true; }
    else if (i == 2) { wifiOn = !wifiOn; prefs.putBool("an", wifiOn); reconnect = true; }
  }
};
static NetMenu menu;

SystemMenu* netMenu() { return &menu; }
