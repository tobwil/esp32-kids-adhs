#!/bin/sh
# Eltern-Portal in der Cloud einrichten (einmalig, du führst das selbst aus):
#   1. bei Cloudflare anmelden (Browser öffnet sich, kostenloses Konto reicht)
#   2. Speicher in der EU anlegen (R2-Bucket „grummel-portal“)
#   3. Gerät-Schlüssel erzeugen und als Geheimnis ablegen, Eltern-Passwort festlegen (wird abgefragt, nicht angezeigt)
#   4. Portal veröffentlichen; am Ende steht die Adresse, die du im Browser und auf dem Gerät einträgst
set -e
cd "$(dirname "$0")"
[ -d node_modules ] || npm install
npx wrangler login
npx wrangler r2 bucket create grummel-portal --jurisdiction eu || echo "(Bucket gibt es schon)"
# Gerät-Schlüssel: zufällig, geht direkt an Cloudflare (steht später im Portal unter „Gerät verbinden“)
openssl rand -hex 24 | tr -d '\n' | npx wrangler secret put GERAET_SCHLUESSEL
echo "Jetzt das Eltern-Passwort für das Portal festlegen (lang und nur für euch):"
npx wrangler secret put ELTERN_PASSWORT
npx wrangler deploy
echo
echo "Fertig. Die Adresse oben (https://grummel-portal.….workers.dev) im Browser öffnen und anmelden."
echo "Gerät verbinden: im Portal unter „Gerät verbinden“ Adresse und Schlüssel kopieren und auf der Einrichtungsseite des Geräts eintragen."
