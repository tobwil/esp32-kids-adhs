// Verbindung zum Eltern-Portal über BSD-Sockets (gibt es auf dem Mac und im ESP-IDF/lwIP), damit Gerät und
// Simulator denselben Code nutzen. Beide Funktionen blockieren und laufen in eigenen Threads/Tasks der Plattform.
#pragma once
#include "radio_player.h"

namespace dk {

// Befehle ans Portal schicken, dann den Zustand abfragen und übernehmen. host: Name oder IP. true = Portal erreichbar
bool portalPoll(RadioPlayer& p, const char* host, int port);

// Strom-Schleife (kehrt nie zurück): öffnet den Strom, den der Player gerade will, und füllt den Ringpuffer.
// host() liefert die aktuelle Adresse (leer = noch kein Netz).
void portalStreamLoop(RadioPlayer& p, const char* (*host)(), int port);

}  // namespace dk
