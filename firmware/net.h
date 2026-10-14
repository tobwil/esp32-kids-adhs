// WLAN, Einrichtung übers Handy und Verbindung zum Eltern-Portal (nur Gerät).
//   - Zugangsdaten liegen im NVS (Namensraum „wlan“), eingegeben werden sie auf einer Seite, die das Gerät selbst
//     anbietet: Einstellungen → „WLAN einrichten“ öffnet das WLAN „Grummel-Setup“ (ohne Passwort, nur für die Einrichtung).
//   - Das Portal findet das Gerät per Bonjour/mDNS (Dienst _grummel._tcp, meldet tools/portal/portal.py an),
//     alternativ über eine feste Adresse aus der Einrichtungsseite.
//   - Zwei Tasks auf Kern 0: Abfrage alle 2 s (portalPoll) und Tonstrom (portalStreamLoop).
#pragma once
#include "app.h"
#include "radio_player.h"

// sd: TF-Karte eingehängt (Offline-Bibliothek: Aufnahmen und Nachrichten werden dorthin kopiert)
void netBegin(dk::RadioPlayer* radio, dk::Messages* msgs, bool sd);
dk::SystemMenu* netMenu();
void netStatus(char* out, int n);   // für den seriellen Befehl „status“
// Portal einstellen (serieller Befehl „portal <adresse> [schlüssel]“): leer = Mac im Heimnetz, https://… = Cloud
void netSetPortal(const char* addr, const char* key);
// Sammelalbum: true = ein Thema aus dem Portal liegt in /album (sonst die eingebauten Pferde)
bool netAlbumTheme();
bool netTakeAlbumReload();          // neues Thema geladen: einmal true
bool netTakeQuizReload();           // neue Rätsel-Fragen in /quiz.txt: einmal true
