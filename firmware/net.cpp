#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "portal_net.h"

using namespace dk;

static constexpr const char* SETUP_SSID = "Grummel-Setup";
static constexpr int DEFAULT_PORT = 8770;

static RadioPlayer* radio = nullptr;
static Preferences prefs;
static volatile bool wifiOn = true, setupOn = false, setupStop = false, setupStart = false;
static volatile bool reconnect = false;
static char ssid[33] = "", portalHost[64] = "";   // portalHost: fest eingestellte Adresse (leer = über mDNS suchen)
static char portalIp[40] = "";                    // gefundene IP des Portals
static int portalPort = DEFAULT_PORT;
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
       "<label>Eltern-Portal (leer lassen = automatisch finden)</label><input name=a placeholder='z. B. HomeBook.local' autocapitalize=off value=\"";
  h += esc(String(portalHost));
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
  portalIp[0] = 0;
  if (!wifiOn || !ssid[0]) { WiFi.mode(WIFI_OFF); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("grummel");
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, prefs.getString("pw", "").c_str());
}

static void findPortal() {
  lastFind = millis();
  static bool mdnsUp = false;
  if (!mdnsUp) mdnsUp = MDNS.begin("grummel");
  portalPort = DEFAULT_PORT;
  if (portalHost[0]) {
    // feste Adresse: „name.local[:port]“ über mDNS, sonst Name oder IP direkt
    char h[64];
    snprintf(h, sizeof h, "%s", portalHost);
    if (char* c = strchr(h, ':')) { portalPort = atoi(c + 1); *c = 0; }
    const size_t n = strlen(h);
    if (n > 6 && !strcasecmp(h + n - 6, ".local")) {
      h[n - 6] = 0;
      const IPAddress ip = MDNS.queryHost(h, 2000);
      if (ip != IPAddress(0, 0, 0, 0)) snprintf(portalIp, sizeof portalIp, "%s", ip.toString().c_str());
    } else {
      snprintf(portalIp, sizeof portalIp, "%s", h);
    }
    return;
  }
  const int n = MDNS.queryService("grummel", "tcp");
  if (n > 0) {
    snprintf(portalIp, sizeof portalIp, "%s", MDNS.address(0).toString().c_str());
    portalPort = MDNS.port(0);
  }
}

static const char* streamHost() { return portalIp; }

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
    if (!portalIp[0] || (!radio->online() && millis() - lastFind > 30000)) findPortal();
    if (!portalIp[0]) { radio->setOnline(false); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
    portalPoll(*radio, portalIp, portalPort);
    vTaskDelay(pdMS_TO_TICKS(radio->playing() ? 1000 : 2000));
  }
}

static void streamTask(void*) { portalStreamLoop(*radio, streamHost, portalPort); }

void netBegin(RadioPlayer* r) {
  radio = r;
  prefs.begin("wlan", false);
  wifiOn = prefs.getBool("an", true);
  snprintf(ssid, sizeof ssid, "%s", prefs.getString("ssid", "").c_str());
  snprintf(portalHost, sizeof portalHost, "%s", prefs.getString("portal", "").c_str());
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(streamTask, "radio", 6144, nullptr, 2, nullptr, 0);
}

void netStatus(char* out, int n) {
  snprintf(out, n, "wlan %s, netz „%s“, ip %s, portal %s:%d %s", !wifiOn ? "aus" : setupOn ? "einrichten" : WiFi.status() == WL_CONNECTED ? "verbunden" : "getrennt",
           ssid, WiFi.localIP().toString().c_str(), portalIp[0] ? portalIp : "-", portalPort, radio && radio->online() ? "erreichbar" : "nicht erreichbar");
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
      case 3: snprintf(b, n, "Eltern-Portal: %s", radio && radio->online() ? "verbunden" : "nicht gefunden"); break;
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
